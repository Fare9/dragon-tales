#!/usr/bin/env python3
"""
pe_report.py -- dependency-free PE analyzer for VMProtect study.

Reports, for a Windows PE (.exe/.dll):
  - machine / bitness, entry point RVA, image size
  - section table with raw+virtual size, characteristics, and Shannon entropy
  - overlay size (data appended after the last section)
  - imported DLLs and function names (from the import directory)
  - simple heuristics that flag VMProtect-style protection

Usage:
  python pe_report.py <file.exe> [--json out.json]

No third-party modules required (no pefile). Works on PE32 and PE32+.
"""

from __future__ import annotations

import sys, struct, math, json, argparse
from typing import Any, Optional

def entropy(b: bytes) -> float:
    """Shannon entropy of `b` in bits per byte, 0.0 to 8.0.

    A packed or encrypted section sits near 8.0; ordinary code is well under 7.
    That gap is what the VMProtect heuristic below keys on.
    """
    if not b:
        return 0.0
    freq=[0]*256
    for x in b:
        freq[x]+=1
    n=len(b); e=0.0
    for c in freq:
        if c:
            p=c/n
            e-=p*math.log2(p)
    return e  # 0..8 bits/byte

CHAR_FLAGS=[(0x20,"CODE"),(0x40,"IDATA"),(0x80,"UDATA"),
            (0x20000000,"EXEC"),(0x40000000,"READ"),(0x80000000,"WRITE")]

def decode_char(ch: int) -> str:
    """Render a section's characteristics bitmask as `CODE|EXEC|READ`."""
    return "|".join(n for m,n in CHAR_FLAGS if ch & m) or "-"

class PE:
    """A minimal, dependency-free PE reader.

    Deliberately not `pefile`: the analysis scripts need only sections, entropy,
    imports and RVA translation, and keeping it dependency-free means the
    examples run with nothing installed beyond cffi, capstone and z3.
    """

    def __init__(self, path: str) -> None:
        """Parse the PE at `path`. Raises ValueError if it is not one."""
        self.path=path
        self.data=open(path,"rb").read()
        d=self.data
        if d[:2]!=b"MZ":
            raise ValueError("not MZ")
        self.e_lfanew=struct.unpack_from("<I",d,0x3C)[0]
        if d[self.e_lfanew:self.e_lfanew+4]!=b"PE\x00\x00":
            raise ValueError("no PE signature")
        coff=self.e_lfanew+4
        self.machine,self.nsec,_,_,_,self.opt_size,_=struct.unpack_from("<HHIIIHH",d,coff)
        self.opt_off=coff+20
        self.magic=struct.unpack_from("<H",d,self.opt_off)[0]
        self.pe32plus=(self.magic==0x20b)
        self.entry=struct.unpack_from("<I",d,self.opt_off+16)[0]
        self.image_base=struct.unpack_from("<Q" if self.pe32plus else "<I",d,self.opt_off+24)[0]
        self.size_image=struct.unpack_from("<I",d,self.opt_off+56)[0]
        # data directories: offset differs by magic
        dd_off=self.opt_off+(112 if self.pe32plus else 96)
        self.ndd=struct.unpack_from("<I",d,self.opt_off+(108 if self.pe32plus else 92))[0]
        self.datadirs=[struct.unpack_from("<II",d,dd_off+i*8) for i in range(self.ndd)]
        self.sec_off=self.opt_off+self.opt_size
        self.sections=[]
        end_raw=0
        for i in range(self.nsec):
            o=self.sec_off+i*40
            name=d[o:o+8].rstrip(b"\x00").decode("latin1","replace")
            vsize,vaddr,rsize,raddr=struct.unpack_from("<IIII",d,o+8)
            ch=struct.unpack_from("<I",d,o+36)[0]
            body=d[raddr:raddr+rsize] if rsize else b""
            self.sections.append(dict(name=name,vsize=vsize,vaddr=vaddr,rsize=rsize,
                                      raddr=raddr,char=ch,entropy=round(entropy(body),3)))
            if rsize:
                end_raw=max(end_raw,raddr+rsize)
        self.overlay=len(d)-end_raw if len(d)>end_raw else 0

    def rva_to_off(self, rva: int) -> Optional[int]:
        """File offset for a relative virtual address, or None if unmapped.

        This is the workhorse of every analysis script here: addresses come out
        of disassembly as VAs, and reading the bytes behind them means going
        VA -> RVA (minus image_base) -> file offset.
        """
        for s in self.sections:
            if s["vaddr"]<=rva<s["vaddr"]+max(s["vsize"],s["rsize"]):
                return s["raddr"]+(rva-s["vaddr"])
        return None

    def imports(self) -> dict[str, list[str]]:
        """Imported symbols by DLL name, from data directory 1.

        Returns {} when there is no import table -- which is itself a signal:
        protectors commonly strip or rebuild it.
        """
        # data directory index 1 = import table
        if self.ndd<2 or self.datadirs[1][1]==0:
            return {}
        rva,_=self.datadirs[1]
        off=self.rva_to_off(rva)
        if off is None: return {}
        d=self.data; res={}
        psz=8 if self.pe32plus else 4
        ORD=1<<(63 if self.pe32plus else 31)
        while True:
            oft,tstamp,fwd,namerva,firstthunk=struct.unpack_from("<IIIII",d,off)
            off+=20
            if namerva==0 and oft==0 and firstthunk==0:
                break
            no=self.rva_to_off(namerva)
            if no is None: break
            dllname=d[no:d.index(b"\x00",no)].decode("latin1","replace")
            funcs=[]
            thunk_rva=oft or firstthunk
            to=self.rva_to_off(thunk_rva)
            if to is not None:
                while True:
                    val=struct.unpack_from("<Q" if self.pe32plus else "<I",d,to)[0]
                    if val==0: break
                    if val & ORD:
                        funcs.append("Ordinal#%d"%(val & 0xffff))
                    else:
                        fo=self.rva_to_off(val & 0x7fffffff)
                        if fo is not None:
                            nm=d[fo+2:d.index(b"\x00",fo+2)].decode("latin1","replace")
                            funcs.append(nm)
                    to+=psz
            res[dllname]=funcs
        return res

    def heuristics(self) -> list[str]:
        """Human-readable notes on anything that looks protected or packed."""
        flags=[]
        for s in self.sections:
            n=s["name"].lower()
            if n.startswith(".vmp") or "vmp" in n:
                flags.append(f"section '{s['name']}' looks like a VMProtect section")
            if s["entropy"]>=7.0 and s["rsize"]>0x400:
                flags.append(f"section '{s['name']}' high entropy {s['entropy']} (packed/encrypted?)")
            if (s["char"]&0x20000000) and (s["char"]&0x80000000):
                flags.append(f"section '{s['name']}' is RWX (executable+writable) -- common for VM/packers")
        # entry point outside first code section?
        if self.sections:
            first=self.sections[0]
            if not (first["vaddr"]<=self.entry<first["vaddr"]+first["vsize"]):
                flags.append(f"entry point RVA {self.entry:#x} not in first section '{first['name']}'")
        if self.overlay>0x100000:
            flags.append(f"large overlay {self.overlay:,} bytes appended after sections")
        return flags

    def report(self) -> dict[str, Any]:
        """Everything print_report() renders, as a plain dict."""
        return dict(
            path=self.path, machine=hex(self.machine),
            bits=64 if self.pe32plus else 32,
            entry_rva=hex(self.entry), image_base=hex(self.image_base),
            size_of_image=self.size_image, file_size=len(self.data),
            overlay=self.overlay,
            sections=self.sections,
            imports={k:len(v) for k,v in self.imports().items()},
            import_detail=self.imports(),
            heuristics=self.heuristics(),
        )

def human(n: int) -> str:
    """Thousands-separated, for the report columns."""
    return f"{n:,}"

def print_report(r: dict[str, Any]) -> None:
    """Print a report() dict in the layout used throughout the write-up."""
    print(f"# {r['path']}")
    print(f"  arch: PE{r['bits']}  machine={r['machine']}  entry={r['entry_rva']}  base={r['image_base']}")
    print(f"  file_size={human(r['file_size'])}  size_of_image={human(r['size_of_image'])}  overlay={human(r['overlay'])}")
    print(f"  {'section':12}{'vsize':>12}{'rawsize':>12}{'entropy':>9}  characteristics")
    for s in r['sections']:
        print(f"  {s['name']:12}{s['vsize']:>12}{s['rsize']:>12}{s['entropy']:>9}  {decode_char(s['char'])}")
    print(f"  imports: {len(r['imports'])} DLL(s), {sum(r['imports'].values())} function(s)")
    for dll,cnt in r['imports'].items():
        print(f"    {dll} ({cnt})")
    if r['heuristics']:
        print("  heuristics:")
        for h in r['heuristics']:
            print(f"    [!] {h}")
    else:
        print("  heuristics: none flagged (looks like a plain, unprotected PE)")

def main() -> None:
    """Report on each PE named on the command line."""
    ap=argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--json")
    a=ap.parse_args()
    pe=PE(a.file)
    r=pe.report()
    print_report(r)
    if a.json:
        with open(a.json,"w") as f:
            json.dump(r,f,indent=2)
        print(f"  [json written to {a.json}]")

if __name__=="__main__":
    main()
