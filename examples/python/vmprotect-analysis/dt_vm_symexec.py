#!/usr/bin/env python3
"""
dt_vm_symexec.py -- symbolically execute the PROTECTED check_serial through the
real VMProtect code, one native instruction at a time, with dragon-tales.

No emulator of the VM is written here: the executor runs VM_ENTRY, the
dispatcher, the handlers and VM_EXIT exactly as they are in the binary. The PE
image is mapped, the serial bytes are symbolic (constrained to a character
class), everything else is concrete. Where control flow depends on the serial
(a VM_EXIT whose return address was computed from flags), the run forks by
re-executing with the alternative assumed.

Outputs
  - per path: the VM trace (entry keys, handlers with operands, exits), the
    returned EAX as a Z3 term, and a solved serial for EAX == 1 validated
    against the real executable;
  - analysis/vm/vm_paths.json with everything, for the recompiler.

Run: python3 dt_vm_symexec.py [--len 8] [--max-paths 8]
"""

from __future__ import annotations

import sys, os
import json, struct, time, subprocess, collections, argparse
from typing import Any, Callable, Optional

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = HERE  # outputs stay beside the scripts
# vmp_paths resolves everything environment-specific: where dragon.py lives,
# where the analysed binaries live, and where LLVM's tools are.
sys.path.insert(0, HERE)
from vmp_paths import dragon_python_dir, require, target as _target

sys.path.insert(0, dragon_python_dir())
import dragon
from dragon import x86
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from pe_report import PE
import dt_handler_views as V

EXE = _target("serial_check.vmp.exe")
pe = PE(require(EXE))
BASE = pe.image_base
md = Cs(CS_ARCH_X86, CS_MODE_64)
ARCH = dragon.Architecture.X86_64
CHECK_SERIAL = 0x140007300
RET_TO_MAIN = 0x1400073A0  # instruction after `call check_serial` in main
VM_ENTRY, FETCH = 0x1400E5000, 0x1400E5103
HANDLERS = {va: ops for va, ops in V.handler_slots().items()}
SER = 0x600000  # serial buffer
STACK_LO, STACK_HI = 0x7F0000, 0x800000
RSP0 = 0x7FF000
NAMES = {}  # handler va -> opcode class name (from post_views.json)
try:
    _pv = json.load(open(os.path.join(HERE, "post_views.json")))
    NAMES = {int(k, 16): v["name"] for k, v in _pv["handlers"].items()}
except Exception:
    pass


def rd(va: int, n: int) -> bytes:
    """`n` bytes of the image at virtual address `va`; empty if unmapped."""
    o = pe.rva_to_off(va - BASE)
    return pe.data[o : o + n] if o is not None else b""


# ------------------------------------------------------------- instruction cache
DIS = dragon.Disassembler(ARCH)
_cache = {}  # pc -> (block, size, mnemonic, keep)


def decode(pc: int) -> tuple[dragon.IGNILBlock, int, str, dragon.Graph, str]:
    """Decode and lift the single instruction at `pc`, memoized.

    Stepping the VM re-executes the same handler code thousands of times, so
    caching the lift is what makes this tractable at all. The Graph is kept in
    the tuple deliberately: the IGNIL block borrows from it and would dangle if
    it were dropped.

    :return: (block, instruction size, mnemonic, owning graph, operand text).
    """
    if pc in _cache:
        return _cache[pc]
    ins = next(md.disasm(rd(pc, 16), pc), None)
    if ins is None:
        raise RuntimeError(f"undecodable at {pc:#x}")
    g = DIS.disassemble(V.canonical_bytes(ins), pc)
    di = g.get_instruction(pc)
    if di is None:
        raise RuntimeError(
            f"dragon cannot decode {ins.mnemonic} {ins.op_str} at {pc:#x}"
        )
    blk = DIS.lift_trace([di])
    kind = ins.mnemonic
    _cache[pc] = (blk, ins.size, kind, g, ins.op_str)
    return _cache[pc]


class Succ:
    """One feasible successor of a branch: where it goes, and under what."""

    def __init__(self, address: int, condition) -> None:
        self.address = address
        self.condition = condition


CONTROL = {"jmp", "call", "ret"} | {
    m
    for m in (
        "ja",
        "jae",
        "jb",
        "jbe",
        "jc",
        "je",
        "jz",
        "jne",
        "jnz",
        "jg",
        "jge",
        "jl",
        "jle",
        "jo",
        "jno",
        "js",
        "jns",
        "jp",
        "jnp",
        "jcxz",
        "jecxz",
        "jrcxz",
    )
}


# ------------------------------------------------------------- machine setup
def fresh_machine(L: int, concrete: Optional[str] = None) -> dragon.SymbolicExecutor:
    """A machine with the image mapped and the serial either symbolic or fixed.

    :param L: serial length, when the serial is symbolic.
    :param concrete: if given, the serial is mapped as these bytes instead and
        the whole run stays concrete -- no solving, much faster.
    """
    # symbolize_unmapped_reads=False is the key to getting past VMProtect's
    # environment init: the bytecode reads gs:[0x60] (the PEB) and other
    # segment-relative locations, which dragon flattens to small absolute
    # addresses (0x18/0x60/0x168/...). Left as fresh symbols they poison VIP a
    # few instructions later (the documented emu.py blocker); forced to concrete
    # zero they read as "no debugger, image not relocated", which is benign, and
    # VIP stays concrete while the serial stays symbolic.
    ex = dragon.SymbolicExecutor(
        stop_at_terminator=False,
        solver_timeout_ms=5000,
        max_branch_targets=8,
        symbolize_unmapped_reads=False,
    )
    for s in pe.sections:
        data = pe.data[s["raddr"] : s["raddr"] + s["rsize"]] if s["rsize"] else b""
        vsz = max(s["vsize"], s["rsize"])
        data = data + bytes(vsz - len(data)) if len(data) < vsz else data[:vsz]
        ex.map_memory(BASE + s["vaddr"], data)
    ex.map_memory(BASE, pe.data[:0x1000])
    ex.map_memory(0, bytes(0x10000))  # flattened gs:[..] env reads -> 0
    ex.map_memory(STACK_LO, bytes(STACK_HI - STACK_LO))
    ex.map_memory(RSP0, struct.pack("<Q", RET_TO_MAIN))
    ex.map_memory(SER - 0x100, bytes(0x300))
    if concrete is not None:
        ex.map_memory(SER, concrete.encode("latin1") + b"\x00")
    else:
        ex.symbolize_memory(SER, L, "serial")
        for i in range(L):
            b = ex.read_memory(SER + i, 8)
            ex.assume(b.uge(0x30))
            ex.assume(b.ule(0x7A))
            ex.assume(b != 0x2D)
    for r in V.GPRS:
        ex.set_register(r, 0)
    ex.set_register(x86.RCX, SER)
    ex.set_register(x86.RSP, RSP0)
    ex.set_register(x86.RBP, 0x7FF800)
    for f in (x86.CF, x86.PF, x86.AF, x86.ZF, x86.SF, x86.OF, x86.DF):
        ex.set_register(f, 0)
    return ex


# ------------------------------------------------------------- one path
class Fork(Exception):
    """Raised at a *new* branch point, carrying the alternatives not taken.

    This is what turns one execution into a search: the caller catches it,
    appends a different choice, and re-runs from the start. Re-running rather
    than snapshotting keeps the engine simple at the cost of redoing the
    prefix -- which is also why the trace-acquisition cost is what it is.
    """


def run_path(L: int, choices: list[int], max_steps: int = 2000000,
             verbose: bool = False,
             probe: Optional[Callable[..., None]] = None,
             concrete: Optional[str] = None) -> dict[str, Any]:
    """Execute from check_serial with the given fork choices. Returns a dict
    (trace, eax, forks_seen, ...) or raises Fork with the alternatives list."""
    ex = fresh_machine(L, concrete=concrete)
    pc = CHECK_SERIAL
    steps = 0
    fork_idx = 0
    trace = []
    conds = []
    t0 = time.perf_counter()

    def resolve(cands: list[Succ], where: int) -> Succ:
        """Pick among feasible alternatives according to `choices`; schedule the
        others by raising Fork when this is a new fork point."""
        nonlocal fork_idx, choices
        if len(cands) == 1:
            return cands[0]
        if fork_idx < len(choices):
            pick = choices[fork_idx]
        else:
            choices = list(choices) + [0]
            if verbose:
                print(
                    f"   [fork] step {steps} at {where:#x}: {[hex(c.address) for c in cands]}"
                )
                for t in trace[-4:]:
                    print(
                        "      ",
                        [hex(x) if isinstance(x, int) and x > 0xFFFF else x for x in t],
                    )
            raise Fork(
                (
                    choices,
                    [choices[:-1] + [i] for i in range(1, len(cands))],
                    where,
                    [f"{c.address:#x}" for c in cands],
                )
            )
        fork_idx += 1
        return cands[pick]

    while steps < max_steps:
        if pc == RET_TO_MAIN:
            eax = ex.read_register(x86.EAX)
            return dict(
                ex=ex,
                eax=eax,
                trace=trace,
                steps=steps,
                conds=conds,
                secs=time.perf_counter() - t0,
                choices=list(choices),
            )
        if not (BASE <= pc < BASE + pe.size_image):
            raise RuntimeError(f"left the image at {pc:#x} after {steps} steps")
        blk, size, kind, _, opstr = decode(pc)
        if probe:
            probe(pc, ex, steps, trace, kind)
        # VM-level events. A VM branch is a VM_EXIT whose re-entry KEY was
        # computed from the flags: the key at [rsp+8] is where paths split.
        if pc == VM_ENTRY:
            rsp = ex.evaluate(ex.read_register(x86.RSP))
            if rsp is None:
                raise RuntimeError("RSP went symbolic at VM_ENTRY")
            k = ex.read_memory(rsp + 8, 32)
            kv = ex.evaluate(k)
            if kv is None:
                vals, _ = ex.enumerate(k, 8)
                if not vals:
                    raise RuntimeError(f"unresolvable entry key at step {steps}")
                s = resolve([Succ(v, k == v) for v in vals], pc)
                ex.assume(s.condition)
                conds.append((pc, f"key == {s.address:#x}"))
                trace.append(
                    (
                        "branch",
                        pc,
                        "key",
                        [f"{v:#x}" for v in vals],
                        vals.index(s.address),
                    )
                )
                kv = s.address
            trace.append(("vm_entry", kv))
        elif pc in HANDLERS:
            vip = ex.evaluate(ex.read_register(x86.RSI))
            op = rd(vip - 1, 1)[0] if vip is not None else None
            trace.append(("handler", pc, op, vip, NAMES.get(pc, "?")))
        steps += 1
        # call / ret: dragon's CALL/RET ops carry control flow only, so the
        # return-address push/pop is done here, concretely.
        if kind == "call":
            rsp = ex.evaluate(ex.read_register(x86.RSP))
            if rsp is None:
                raise RuntimeError(f"RSP went symbolic at call {pc:#x}")
            rsp -= 8
            ex.set_register(x86.RSP, rsp)
            ex.map_memory(rsp, struct.pack("<Q", pc + size))
            try:
                pc = int(opstr, 16)
            except ValueError:
                tgt = ex.evaluate(ex.read_register(getattr(x86, opstr.upper())))
                if tgt is None:
                    raise RuntimeError(f"symbolic call target at {pc:#x}")
                pc = tgt
            continue
        if kind == "ret":
            rsp = ex.evaluate(ex.read_register(x86.RSP))
            if rsp is None:
                raise RuntimeError(f"RSP went symbolic at ret {pc:#x}")
            m = ex.read_memory(rsp, 64)
            ex.set_register(x86.RSP, rsp + 8)
            tgt = ex.evaluate(m)
            if tgt is not None:
                pc = tgt
                continue
            vals, exhaustive = ex.enumerate(m, 8)
            if not vals:
                raise RuntimeError(
                    f"unresolvable ret target at {pc:#x}: {str(m)[:200]}"
                )
            succ = [Succ(v, m == v) for v in vals]
        else:
            ex.set_register(x86.RIP, pc + size)
            res = ex.run(blk)
            if not res.ok:
                raise RuntimeError(f"executor error at {pc:#x}: {res.error}")
            succ = res.successors if kind in CONTROL else None
        if succ is not None:
            if not succ:
                raise RuntimeError(f"no successor at {pc:#x} ({kind})")
            feasible = []
            for s in succ:
                if s.condition is None or ex.is_satisfiable(s.condition):
                    feasible.append(s)
            if not feasible:
                raise RuntimeError(f"no feasible successor at {pc:#x}")
            s = resolve(feasible, pc)
            if s.condition is not None and len(succ) > 1:
                ex.assume(s.condition)
                conds.append((pc, str(s.condition)[:200]))
            if len(feasible) > 1:
                trace.append(
                    (
                        "branch",
                        pc,
                        kind,
                        [f"{x.address:#x}" for x in feasible],
                        feasible.index(s),
                    )
                )
            pc = s.address
        else:
            pc += size
        if verbose and steps % 20000 == 0:
            print(
                f"   ... {steps} steps, {len(trace)} VM events, {time.perf_counter()-t0:.0f}s",
                flush=True,
            )
    raise RuntimeError("step limit")


def _handler_of(pc: int) -> Optional[int]:
    """The VM handler `pc` belongs to, or None if it is not in one."""
    """handler entry that contains pc (handlers are contiguous-ish; use the largest entry <= pc)"""
    best = None
    for va in HANDLERS:
        if va <= pc and (best is None or va > best):
            best = va
    return best


# ------------------------------------------------------------- driver
def validate(serial: str) -> "bool | str":
    """Run the real binary on `serial`.

    :return: True/False for granted/denied, or an error string if it could not
        be run at all (wrong platform, binary missing).
    """
    try:
        out = subprocess.run([EXE, serial], capture_output=True, text=True, timeout=10)
        return "granted" in out.stdout
    except Exception as e:
        return f"error: {e}"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--len", type=int, default=8)
    ap.add_argument("--max-paths", type=int, default=8)
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    queue = [[]]
    done = []
    seen = set()
    while queue and len(done) < a.max_paths:
        choices = queue.pop(0)
        key = tuple(choices)
        if key in seen:
            continue
        seen.add(key)
        print(f"== path choices={choices}", flush=True)
        try:
            r = run_path(a.len, choices, verbose=a.verbose)
        except Fork as f:
            this, alts, pc, targets = f.args[0]
            print(
                f"   fork at {pc:#x} -> {targets}; scheduling {len(alts)} alternative(s)"
            )
            queue.insert(0, this)
            for alt in alts:
                queue.append(alt)
            continue
        ex = r["ex"]
        handlers = [t for t in r["trace"] if t[0] == "handler"]
        entries = [t for t in r["trace"] if t[0] == "vm_entry"]
        print(
            f"   done: {r['steps']} native steps, {len(handlers)} VM instructions, "
            f"{len(entries)} VM entries, {r['secs']:.1f}s"
        )
        print(f"   EAX = {V.pretty(str(r['eax']))[:300]}")
        sol = ex.solve(r["eax"] == 1)
        serial = None
        if sol:
            serial = sol.bytes("serial").decode("latin1")
            print(
                f"   EAX==1 satisfiable: serial {serial!r} -> real exe: {validate(serial)}"
            )
        else:
            sol0 = ex.solve(r["eax"] == 0)
            print(
                f"   EAX==1 unsatisfiable on this path; EAX==0 {'satisfiable' if sol0 else 'unsat'}"
            )
        done.append(
            dict(
                choices=r["choices"],
                steps=r["steps"],
                vm_instructions=len(handlers),
                entries=[k for _, k in entries],
                conds=r["conds"],
                eax=V.pretty(str(r["eax"])),
                serial=serial,
                trace=[list(t) for t in r["trace"]],
            )
        )
    # summary of the VM program shape
    if done:
        counts = collections.Counter(
            t[4] for p in done for t in p["trace"] if t[0] == "handler"
        )
        print("\nVM instruction mix over all paths:", counts.most_common(15))
    json.dump(
        dict(len=a.len, paths=done),
        open(os.path.join(HERE, "vm_paths.json"), "w"),
        indent=1,
    )
    print(f"wrote vm_paths.json ({len(done)} paths)")


if __name__ == "__main__":
    main()
