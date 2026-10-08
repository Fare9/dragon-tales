#!/usr/bin/env python3
"""
dt_catalog.py -- batch-devirtualize every VMProtect handler with dragon-tales
and emit an annotated markdown catalog (analysis/vm/HANDLERS.md).

For each of the 256 handler-table slots: extract the handler (neutralizing its
tail jump back to the dispatcher so it lifts as one function), lift to LLVM IR,
optimize (mem2reg/instcombine/gvn/dce), normalize the IR to a semantic
signature, and group handlers that share a signature. VMProtect emits many
polymorphic copies of a small opcode set, so grouping collapses 256 slots to
the real opcode catalog.

Run: python3 dt_catalog.py

Note: this is the *first-pass* catalogue, grouping handlers by the shape of
their lifted IR. It mislabeled several groups and left a third of the slots
unnamed, which is why dt_handler_views.py exists -- that one names handlers by
their recovered symbolic *effect* instead. Kept because grouping by IR shape is
still the fastest way to see how many distinct handlers a build really has.
"""

from __future__ import annotations

import sys, os
import re, struct, collections
from typing import Optional

# vmp_paths resolves everything environment-specific: where dragon.py lives,
# where the analysed binaries live, and where LLVM's tools are.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vmp_paths import dragon_python_dir, require, target as _target

sys.path.insert(0, dragon_python_dir())
import dragon
from pe_report import PE
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

EXE = _target("serial_check.vmp.exe")
TABLE = 0x1400E93F0
DISPATCH = {0x1400E5103, 0x1400E511C}
REGROLE = {
    32: "regfile",
    40: "vm_stack",
    48: "vip",
    24: "t",
    56: "t",
    64: "t",
    72: "t",
    8: "t",
    0: "t",
    16: "t",
}

pe = PE(require(EXE))
base = pe.image_base
md = Cs(CS_ARCH_X86, CS_MODE_64)


def rd(va: int, n: int) -> bytes:
    """`n` bytes of the image at virtual address `va`."""
    o = pe.rva_to_off(va - base)
    return pe.data[o : o + n] if o is not None else b""


def handler_vas() -> "collections.OrderedDict[int, list[int]]":
    """Handler address -> the opcode slots that dispatch to it.

    The table holds rel32 displacements from its own base, so 256 slots collapse
    to however many distinct handlers the build actually emitted.
    """
    out = collections.OrderedDict()
    for op in range(256):
        d = rd(TABLE + op * 4, 4)
        if len(d) < 4:
            continue
        h = (TABLE + struct.unpack("<i", d)[0]) & 0xFFFFFFFFFFFFFFFF
        out.setdefault(h, []).append(op)
    return out


def extract_patched(va: int, window: int = 0x400) -> Optional[bytes]:
    """Handler bytes with its tail jump back to the dispatcher patched to `ret`.

    Every handler ends by jumping into the fetch/dispatch block, so lifting one
    verbatim drags the whole VM in behind it. Rewriting that jump to a return
    makes the handler a self-contained function LLVM can optimize in isolation.
    """
    raw = bytearray(rd(va, window))
    end = None
    for ins in md.disasm(bytes(raw), va):
        rel = ins.address - va
        if ins.mnemonic == "ret":
            end = rel + ins.size
            break
        if ins.mnemonic == "jmp":
            try:
                tgt = int(ins.op_str, 16)
            except:
                tgt = None
            if tgt in DISPATCH:
                raw[rel] = 0xC3
                end = rel + 1
                break
        if rel > window - 16:
            break
    return bytes(raw[:end]) if end else None


def devirt(va: int) -> Optional[str]:
    """Lift one handler to optimized LLVM IR, or None if it cannot be lifted."""
    code = extract_patched(va)
    if code is None:
        return None
    if not code:
        return None
    try:
        dis = dragon.Disassembler(
            dragon.Architecture.X86_64,
            dragon.Configuration(strategy=dragon.Strategy.RECURSIVE_TRAVERSAL),
        )
        g = dis.analyze(code, va)
        fa = g.function_addresses()
        if not fa:
            return None
        L = dragon.LLVMLifter()
        if not L.lift(g.get_function(fa[0]), g):
            return None
        L.optimize()
        return L.ir()
    except Exception:
        return None


def body_of(ir: Optional[str]) -> str:
    """Just the function body of an LLVM module, without the header.

    Accepts None (a handler that would not lift) and returns "", so callers can
    treat "did not lift" and "lifted to nothing" the same way.
    """
    if not ir or "{" not in ir:
        return ""
    return ir.split("{", 1)[1].rsplit("}", 1)[0].strip()


ROLE = {32: "regfile", 40: "vm_stack", 48: "vip"}
FLAG_MIN = 128  # regs offset >= 128 are flags (CF/ZF/...) -> ignore


def parse_defs(body: str) -> dict[str, str]:
    """Map each SSA name in `body` to the text of the instruction defining it.

    Enough of an SSA index to walk def-use chains backwards, which is all the
    role resolution below needs -- no real IR parser required.
    """
    defs = {}
    for l in body.splitlines():
        l = (
            l.strip()
            .replace(", align 4", "")
            .replace(", align 1", "")
            .replace(", align 2", "")
            .replace(", align 8", "")
        )
        m = re.match(r"(%[\w.]+) = (.+)", l)
        if m:
            defs[m.group(1)] = m.group(2)
    return defs


def resolve_ptr(v: str, defs: dict[str, str], depth: int = 0) -> str:
    """Return a role string for a pointer SSA value: vm_stack/regfile/vip/regs[N]/mem[..]."""
    if depth > 8:
        return "?"
    d = defs.get(v)
    if not d:
        return "?"
    m = re.match(r"getelementptr i8, ptr (%[\w.]+), i64 (.+)", d)
    if not m:
        return "?"
    baseptr, idx = m.group(1), m.group(2).strip()
    if baseptr == "%regs":
        try:
            off = int(idx)
        except:
            return "regs[?]"
        if off >= FLAG_MIN:
            return "flag"
        return ROLE.get(off, f"reg{off}")
    if baseptr == "%mem":
        return "mem[" + resolve_val_role(idx, defs, depth + 1) + "]"
    # nested: mem + base then + index
    inner = resolve_ptr(baseptr, defs, depth + 1)
    return inner + "+idx"


def resolve_val_role(v: str, defs: dict[str, str], depth: int = 0) -> str:
    """Coarse role of a value SSA: which VM ptr it loaded from."""
    if depth > 8:
        return "?"
    v = v.strip()
    if re.match(r"^-?\d+$", v):
        return "const"
    d = defs.get(v)
    if not d:
        return "?"
    m = re.match(r"load i\d+, ptr (%[\w.]+)", d)
    if m:
        return resolve_ptr(m.group(1), defs, depth + 1)
    m = re.match(r"(zext|trunc|sext)[^%]*(%[\w.]+)", d)
    if m:
        return resolve_val_role(m.group(2), defs, depth + 1)
    return "expr"


def _clean(l: str) -> str:
    """Drop the `, align N` noise so instruction text compares cleanly."""
    for a in (", align 8", ", align 4", ", align 2", ", align 1"):
        l = l.replace(a, "")
    return l.strip()


def resolve_ptr_role(v: str, defs: dict[str, str], depth: int = 0) -> str:
    """Which VM structure a pointer SSA value points into."""
    if depth > 10:
        return "?"
    d = defs.get(v)
    if not d:
        return "?"
    m = re.match(r"getelementptr i8, ptr (%[\w.]+), i64 (.+)", d)
    if not m:
        return "?"
    baseptr, idx = m.group(1), m.group(2).strip()
    if baseptr == "%regs":
        try:
            off = int(idx)
        except:
            return "regs?"
        if off >= FLAG_MIN:
            return "flag"
        return ROLE.get(off, "scratch")
    if baseptr == "%mem":
        r = (
            resolve_ptr_role(_backptr(idx, defs), defs, depth + 1)
            if idx.startswith("%") and _is_ptr(idx, defs)
            else _val_role(idx, defs, depth + 1)
        )
        return "MEM:" + r
    inner = resolve_ptr_role(baseptr, defs, depth + 1)  # nested mem+base then +index
    return inner + "+idx"


def _is_ptr(v: str, defs: dict[str, str]) -> bool:
    """True if `v` was produced by a getelementptr."""
    d = defs.get(v, "")
    return d.startswith("getelementptr")


def _backptr(v: str, defs: dict[str, str]) -> str:
    """Step back one level through a pointer chain."""
    return v


def _val_role(v: str, defs: dict[str, str], depth: int = 0) -> str:
    """Coarse role of a value: which VM structure it was loaded from."""
    v = v.strip()
    if re.match(r"^-?\d+$", v):
        return "const"
    d = defs.get(v)
    if not d:
        return "?"
    m = re.match(r"load i\d+, ptr (%[\w.]+)", d)
    if m:
        return resolve_ptr_role(m.group(1), defs, depth + 1)
    m = re.match(r"(zext|trunc|sext)[^%]*(%[\w.]+)", d)
    if m:
        return _val_role(m.group(2), defs, depth + 1)
    # pointer +/- constant keeps the pointer's role (push/pop adjust RBP first)
    m = re.match(r"(add|sub) i\d+ (%[\w.]+), (-?\d+)$", d)
    if m:
        return _val_role(m.group(2), defs, depth + 1)
    m = re.match(r"(add|sub) i\d+ (-?\d+), (%[\w.]+)$", d)
    if m:
        return _val_role(m.group(3), defs, depth + 1)
    return "expr"


def val_expr(v: str, defs: dict[str, str], depth: int = 0) -> str:
    """Coarse structure of a stored value: pop / regfile / operand / ALU(...)."""
    if depth > 10:
        return "?"
    v = v.strip()
    if re.match(r"^-?\d+$", v):
        return "imm"
    d = defs.get(v)
    if not d:
        return "?"
    m = re.match(r"load i\d+, ptr (%[\w.]+)", d)
    if m:
        role = resolve_ptr_role(m.group(1), defs, depth + 1)
        if role == "MEM:vm_stack":
            return "pop"
        if role.startswith("MEM:regfile"):
            return "regfile"
        if role == "MEM:vip":
            return "operand"
        if role.startswith("MEM"):
            return "mem"
        return role
    m = re.match(r"(zext|trunc|sext)[^%]*(%[\w.]+)", d)
    if m:
        return val_expr(m.group(2), defs, depth + 1)
    m = re.match(
        r"(add|sub|mul|xor|and|or|shl|lshr|ashr) i\d+ (%[\w.]+|-?\d+), (%[\w.]+|-?\d+)",
        d,
    )
    if m:
        op = m.group(1)
        a = val_expr(m.group(2), defs, depth + 1)
        b = val_expr(m.group(3), defs, depth + 1)
        pair = sorted([a, b]) if op in ("add", "mul", "xor", "and", "or") else [a, b]
        return f"{op}({pair[0]},{pair[1]})"
    m = re.match(r"icmp (\w+) i\d+ (%[\w.]+|-?\d+), (%[\w.]+|-?\d+)", d)
    if m:
        return f"cmp({m.group(1)})"
    return "expr"


def semantics(body: str) -> tuple[str, list[str]]:
    """Recover a handler's data effect from its IR.

    :return: (signature string used to group identical handlers, effect facts).
    """
    defs = parse_defs(body)
    facts = []
    for l in body.splitlines():
        l = _clean(l)
        m = re.match(r"store i(\d+) (%[\w.]+|-?\d+), ptr (%[\w.]+)", l)
        if not m:
            continue
        w = int(m.group(1))
        valtok = m.group(2)
        ptr = m.group(3)
        tgt = resolve_ptr_role(ptr, defs)
        if tgt == "flag" or tgt == "scratch" or tgt in ("regfile", "vm_stack", "vip"):
            # storing back a native reg slot (regs[off]) that is VM ptr state or scratch:
            # keep only real VM ptr updates (sp/vip) as light annotation, skip scratch/flags
            if tgt in ("vm_stack", "vip"):  # sp/vip pointer update
                facts.append(f"{tgt}_update")
            continue
        if tgt.startswith("MEM"):
            dst = (
                "push"
                if tgt == "MEM:vm_stack"
                else "regfile[op]" if tgt.startswith("MEM:regfile") else tgt
            )
            facts.append(f"{dst}={val_expr(valtok,defs)}:{w}")
    sig = repr(sorted(set(facts)))
    return sig, sorted(set(facts))


def describe(body: str) -> str:
    """One-line human description of a handler's effect."""
    _, f = semantics(body)
    return "; ".join(f) if f else "(pointer/flag only)"


def name_opcode(facts: list[str], body: str) -> str:
    """Propose a canonical VM opcode name from the recovered data effect.
    Heuristic + a few IR-shape special cases; refine by hand in Notes."""
    data = [f for f in facts if f not in ("vm_stack_update", "vip_update")]

    def parse(f: str) -> tuple[str, Optional[str], Optional[int]]:
        m = re.match(r"(.+?)=(.+):(\d+)$", f)
        return (m.group(1), m.group(2), int(m.group(3))) if m else (f, None, None)

    entries = [parse(f) for f in data]
    nload = body.count("= load i64, ptr")
    # context restore: reads a long run of consecutive qwords off the VM stack
    if nload >= 10:
        return "VM_EXIT?"
    # multiply/divide: writes both RAX(regs[0]) and RDX(regs[16])
    if (
        "store i64" in body
        and ", ptr %regs, align" in body
        and "i64 16\n  store i64" in body
    ):
        pass
    if (
        re.search(r"store i64 [^\n]+, ptr %regs, align", body)
        and "getelementptr i8, ptr %regs, i64 16" in body
        and any("MEM:vm_stack+idx" in e[0] for e in entries)
    ):
        return "MULDIV?"
    if not data:
        if re.search(r"store i64 %\d+, ptr %0", body) and re.search(
            r"%\d+ = load i64, ptr %2", body
        ):
            return "SET_VSP"
        return "STACK_ADJ?"
    # ALU (arithmetic combining two inputs) — check first, it's the real op
    for lhs, rhs, w in entries:
        if rhs and re.match(r"(add|sub|and|or|xor|mul|shl|lshr|ashr)\(", rhs):
            return {
                "add": "ADD",
                "sub": "SUB",
                "and": "AND",
                "or": "OR",
                "xor": "XOR",
                "mul": "MUL",
                "shl": "SHL",
                "lshr": "SHR",
                "ashr": "SAR",
            }[rhs.split("(")[0]] + str(w)
    for lhs, rhs, w in entries:
        if lhs == "regfile[op]" and rhs == "pop":
            return f"POP_VR{w}"
    for lhs, rhs, w in entries:
        if lhs == "push" and rhs == "regfile":
            return f"PUSH_VR{w}"
    for lhs, rhs, w in entries:
        if lhs == "push" and rhs == "vm_stack":
            return "PUSH_VSP"
    if any(rhs == "imm" for _, rhs, _ in entries):
        w = next(w for _, rhs, w in entries if rhs == "imm")
        return f"PUSH_IMM{w}"
    for lhs, rhs, w in entries:
        if lhs == "push" and rhs == "operand":
            return f"PUSH_IMM{w}"
    for lhs, rhs, w in entries:
        if rhs == "mem":
            return f"LOAD{w}"
    if any(lhs.startswith("MEM:vm_stack+idx") for lhs, _, _ in entries):
        return "STORE?"
    # arithmetic/compare primitive: value is an unresolved expr but the handler
    # assembles an x86 EFLAGS word (CF/ZF/SF/OF -> select 64 / shl 7 / shl 11).
    if (
        "select i1" in body
        and "or disjoint" in body
        and any(rhs in ("expr", "scratch") for _, rhs, _ in entries)
    ):
        return "ARITH_FLAGS?"
    return "UNK"


def main() -> None:
    hv = handler_vas()
    print(f"handlers: {len(hv)} distinct over 256 slots")
    groups = collections.defaultdict(list)  # signature -> [(va, opcodes, body)]
    trivial = []
    ok = 0
    for va, ops in hv.items():
        ir = devirt(va)
        body = body_of(ir)
        sig, _ = semantics(body)
        if not body.strip() or sig == repr([]):
            trivial.append((va, ops))
            continue
        groups[sig].append((va, ops, body))
        ok += 1
    # write markdown
    outp = os.path.join(os.path.dirname(__file__), "HANDLERS.md")
    with open(outp, "w", encoding="utf-8") as f:
        f.write("# VMProtect handler catalog (dragon-tales devirtualization)\n\n")
        f.write(
            f"Target: `build/x64/serial_check.vmp.exe`. {len(hv)} distinct handlers "
            f"over 256 opcode slots, grouped by recovered LLVM-IR semantics "
            f"(dragon-tales lift + mem2reg/instcombine/gvn/dce). "
            f"`regs` offsets: 32=VM register file, 40=VM stack ptr, 48=VIP.\n\n"
        )
        f.write(f"- distinct semantic groups: **{len(groups)}**\n")
        f.write(f"- handlers recovered to non-trivial IR: **{ok}/{len(hv)}**\n")
        f.write(f"- trivial/empty IR (need review): **{len(trivial)}**\n\n")
        f.write(
            "### Legend\n\n"
            "Groups are keyed by the recovered *data effect*; flags, scratch registers\n"
            "and pointer bookkeeping are ignored. Notation:\n\n"
            "- `pop` = read from the VM stack (`mem[RBP]`); `push=...` = write to it.\n"
            "- `regfile[op]` = a virtual register chosen by the next bytecode byte\n"
            "  (`mem[RSP + operand]`); `operand`/`imm` = a value taken from the bytecode.\n"
            "- `regfile[op]=pop:N` = pop an N-bit value into a virtual register.\n"
            "- `add(a,b)`/`sub(a,b)`/`xor(a,b)`... = an ALU handler combining two inputs.\n"
            "- `vm_stack_update`/`vip_update` = the handler advances RBP/RSI (bookkeeping,\n"
            "  present in nearly every handler).\n"
            "- `MEM:expr=...` = a store whose base pointer the tracer could not fully\n"
            "  resolve to a role; read the representative IR and annotate manually.\n\n"
            "Each group lists its member handler addresses and opcode slots, one\n"
            "representative's optimized LLVM IR, and a **Notes** line to fill in the\n"
            "opcode name. VMProtect emits many polymorphic copies per opcode, which is\n"
            "why 256 handlers collapse to far fewer semantic groups. Two dragon-tales\n"
            "lifter fixes made this possible (LifterGaps.md #5 load-direction `*_REV`\n"
            "reg-reg, #6 8/16-bit memory MOV).\n\n"
        )
        ordered = sorted(groups.items(), key=lambda kv: -len(kv[1]))
        # proposed opcode name per group
        named = []
        for sig, members in ordered:
            _, facts = semantics(members[0][2])
            named.append((name_opcode(facts, members[0][2]), sig, members))
        # summary: opcode name -> total handler count + widths
        byname: dict[str, list] = collections.defaultdict(lambda: [0, set(), []])
        for nm, sig, members in named:
            byname[nm][0] += len(members)
            byname[nm][2].append(members[0][0])
        f.write("## Proposed opcode summary\n\n")
        f.write(
            "Auto-named from the recovered data effect (heuristic — verify in each\n"
            "group's Notes). A trailing `?` means low confidence.\n\n"
        )
        f.write("| proposed opcode | handlers | example handler |\n|---|---|---|\n")
        for nm, (cnt, _, exs) in sorted(byname.items(), key=lambda kv: -kv[1][0]):
            f.write(f"| `{nm}` | {cnt} | `{exs[0]:#x}` |\n")
        f.write("\n---\n\n")
        # per-group detail
        for i, (nm, sig, members) in enumerate(named, 1):
            va0, ops0, body0 = members[0]
            allops = sorted(o for _, os_, _ in members for o in os_)
            f.write(f"## Group {i} — proposed: `{nm}`  ({describe(body0)})\n\n")
            f.write(f"- **{len(members)}** handler(s); representative `{va0:#x}`\n")
            f.write(f"- opcodes: `{allops}`\n")
            f.write(
                f"- handler addrs: {', '.join(hex(v) for v,_,_ in members[:12])}"
                f"{' ...' if len(members)>12 else ''}\n\n"
            )
            f.write("Recovered IR (representative):\n```llvm\n" + body0 + "\n```\n\n")
            f.write(f"**Notes:** _(proposed `{nm}` — confirm/rename)_\n\n---\n\n")
        if trivial:
            f.write("## Trivial / empty IR (need manual review)\n\n")
            f.write(
                "These optimized to nothing recoverable in isolation (often pushes "
                "whose store form the lifter drops, or pure control ops). Addrs:\n\n"
            )
            f.write(", ".join(f"`{v:#x}`(op {os_})" for v, os_ in trivial[:60]) + "\n")
    print(f"wrote {outp}: {len(groups)} groups, {ok} recovered, {len(trivial)} trivial")


if __name__ == "__main__":
    main()
