#!/usr/bin/env python3
"""Emit LLVM IR for a recovered CFG, one real instruction per VM opcode.

`dt_vm_cfg.py` recovers the virtual program's shape: basic blocks keyed by VIP,
edges between them, back edges where it loops. This script turns that shape into
an LLVM function -- one LLVM basic block per recovered block, and for each VM
instruction the *real* instructions its opcode means.

The semantics come from the four-view analysis (`post_views.json`): every opcode
class was named by the effect it was proved to have, so `ADD32` here emits an
`add i32` because that is what the handler was shown to do, not because the
mnemonic says so.

Two design notes, both chosen for readability over cleverness:

  * The VM stack and register file are `alloca`s and the emitted code really
    loads and stores through them. A compile-time stack of SSA values would
    emit less, but it has to reconstruct phi nodes at every merge; letting
    `mem2reg` do that is both shorter to read and harder to get wrong. The VM
    stack depth is invariant per program point, so the offsets are constants
    and SROA promotes almost all of it away anyway.
  * Flags are not modelled. VMProtect pushes RFLAGS after each ALU op and
    usually pops it straight into a dead register; where a real flag is
    consumed this emits a zero and says so in a comment. That is the first
    thing to fix if a comparison ever needs to flow through.

What to expect right now: with a small exploration budget the reachable region
is VMProtect's *preamble* -- several VM entries and a long integrity section
before `check_serial`'s own bytecode starts (PROGRESS 2.6 records the same
thing). The emitted function therefore verifies, optimizes, and returns `undef`,
because the virtual register holding RAX is never written in that region. That
is the lifter reporting the truth about what it was given, not a lifter bug: to
get a meaningful function the exploration has to reach past the integrity
section, which is the trace-acquisition problem the write-up calls the wall.

Run: python3 dt_vm_lift.py [--serial AB] [--max-vm 400] [-o out.ll]
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dt_vm_cfg as C
import dt_vm_symexec as S
from vmp_paths import llvm_tool

#: Byte size of the emitted VM stack and register file.
STACK_BYTES = 0x400
REGS_BYTES = 0x200
#: Virtual register holding check_serial's argument (RCX at VM entry).
SERIAL_VREG = 0x80
#: Virtual register holding RAX, i.e. the function's return value.
RAX_VREG = 0xA8


class Fn:
    """A tiny LLVM text builder: SSA names, instructions, basic blocks."""

    def __init__(self, name: str) -> None:
        self.name = name
        self.lines: list[str] = []
        self._n = 0

    def tmp(self) -> str:
        self._n += 1
        return f"%t{self._n}"

    def emit(self, text: str) -> None:
        self.lines.append("  " + text)

    def label(self, name: str) -> None:
        self.lines.append(f"{name}:")

    def comment(self, text: str) -> None:
        self.lines.append(f"  ; {text}")

    def inst(self, expr: str) -> str:
        """Emit `%t = <expr>` and return the new name."""
        t = self.tmp()
        self.emit(f"{t} = {expr}")
        return t


class VMState:
    """The emitted machine: a stack pointer, a stack, and a register file.

    Everything here emits IR rather than computing -- `pop()` returns the SSA
    name of a loaded value, not a value.
    """

    def __init__(self, f: Fn) -> None:
        self.f = f

    # -- addressing ---------------------------------------------------------
    def _vsp(self) -> str:
        return self.f.inst("load i64, ptr %vsp")

    def _set_vsp(self, v: str) -> None:
        self.f.emit(f"store i64 {v}, ptr %vsp")

    def _stack_at(self, off: str) -> str:
        return self.f.inst(f"getelementptr i8, ptr %stack, i64 {off}")

    def reg_at(self, idx: int) -> str:
        return self.f.inst(f"getelementptr i8, ptr %regs, i64 {idx}")

    # -- the VM stack -------------------------------------------------------
    def push(self, value: str, width: int) -> None:
        """Push `value`, which is `width` bits wide, growing the stack down."""
        n = max(2, width // 8)          # 8-bit values travel in 16-bit slots
        sp = self._vsp()
        new = self.f.inst(f"sub i64 {sp}, {n}")
        self._set_vsp(new)
        p = self._stack_at(new)
        self.f.emit(f"store i{width} {value}, ptr {p}")

    def pop(self, width: int) -> str:
        """Pop a `width`-bit value and return its SSA name."""
        n = max(2, width // 8)
        sp = self._vsp()
        p = self._stack_at(sp)
        v = self.f.inst(f"load i{width}, ptr {p}")
        self._set_vsp(self.f.inst(f"add i64 {sp}, {n}"))
        return v


# ---------------------------------------------------------------------------
# One emitter per opcode family. Each takes (state, width, operand) and emits
# the real instructions the handler was proved to perform.
# ---------------------------------------------------------------------------

def _binop(st: VMState, width: int, op: str) -> None:
    """Pop two operands, push `op` of them. The flags word is pushed as zero."""
    b = st.pop(width)
    a = st.pop(width)
    st.push(st.f.inst(f"{op} i{width} {a}, {b}"), width)
    st.f.comment("RFLAGS pushed as 0 (flags are not modelled)")
    st.push("0", 64)


def emit(st: VMState, name: str, width: int, operand: int) -> bool:
    """Emit one VM instruction. Returns False if the class is not handled."""
    f = st.f

    if name.startswith("PUSH_IMM"):
        st.push(str(operand), width)
    elif name.startswith("PUSH_VR"):
        p = st.reg_at(operand)
        st.push(f.inst(f"load i{width}, ptr {p}"), width)
    elif name.startswith("POP_VR"):
        v = st.pop(width)
        f.emit(f"store i{width} {v}, ptr {st.reg_at(operand)}")
    elif name.startswith("LOAD"):
        addr = st.pop(64)
        p = f.inst(f"inttoptr i64 {addr} to ptr")
        st.push(f.inst(f"load i{width}, ptr {p}"), width)
    elif name.startswith("STORE"):
        addr = st.pop(64)
        v = st.pop(width)
        p = f.inst(f"inttoptr i64 {addr} to ptr")
        f.emit(f"store i{width} {v}, ptr {p}")
    elif name.startswith("PUSH_VSP"):
        st.push(st._vsp(), width)
    elif name.startswith("SET_VSP"):
        st._set_vsp(st.pop(64))
    elif name.startswith("ADD"):
        _binop(st, width, "add")
    elif name.startswith("SUB"):
        _binop(st, width, "sub")
    elif name.startswith("SHL"):
        _binop(st, width, "shl")
    elif name.startswith("SHR"):
        _binop(st, width, "lshr")
    elif name.startswith("SAR"):
        _binop(st, width, "ashr")
    elif name.startswith("NOR"):
        # not a; not b; and  ->  ~(a|b). instcombine applies De Morgan and
        # folds NOR/NAND pairs back into the xor the source really had.
        b, a = st.pop(width), st.pop(width)
        o = f.inst(f"or i{width} {a}, {b}")
        st.push(f.inst(f"xor i{width} {o}, -1"), width)
        st.push("0", 64)
    elif name.startswith("NAND"):
        b, a = st.pop(width), st.pop(width)
        n = f.inst(f"and i{width} {a}, {b}")
        st.push(f.inst(f"xor i{width} {n}, -1"), width)
        st.push("0", 64)
    elif name.startswith("NOT"):
        v = st.pop(width)
        st.push(f.inst(f"xor i{width} {v}, -1"), width)
    elif name.startswith("MUL") or name.startswith("IMUL"):
        _binop(st, width, "mul")
    elif name.startswith("DIV"):
        _binop(st, width, "udiv")
    elif name == "VM_EXIT":
        # In the real VM this pops the whole saved native context off the VM
        # stack and returns to the caller. This function never had a VM_ENTRY
        # to push that context, so reading the context slot would read an
        # untouched alloca. The return value is taken from the virtual register
        # the prologue put RAX in instead -- the bytecode's 17 opening POP_VR64
        # give that mapping (see the write-up's bytecode decode).
        v = f.inst(f"load i64, ptr {st.reg_at(RAX_VREG)}")
        f.emit(f"ret i32 {f.inst(f'trunc i64 {v} to i32')}")
        return True
    elif name in ("CPUID", "RDTSC", "RDTSCP"):
        # Opaque by construction -- exactly what IGNIL's OPAQUE models.
        for i in range(4 if name == "CPUID" else 2):
            st.push(f.inst(f"call i32 @dragon.opaque.{name.lower()}.{i}()"), 32)
    else:
        return False
    return True


def lift(blocks: dict[int, C.VMBlock], lengths: dict[int, int],
         entry: int, back_edges: set[tuple[int, int]]) -> tuple[str, list[str]]:
    """Emit an LLVM function for the recovered CFG.

    :return: (module text, opcode classes that had no emitter).
    """
    f = Fn("check_serial_devirt")
    st = VMState(f)
    missing: list[str] = []

    f.label("entry")
    f.emit(f"%stack = alloca [{STACK_BYTES} x i8]")
    f.emit(f"%regs  = alloca [{REGS_BYTES} x i8]")
    f.emit("%vsp   = alloca i64")
    f.comment("the VM stack grows down from the top of the allocation")
    f.emit(f"store i64 {STACK_BYTES - 0x40}, ptr %vsp")
    f.comment(f"check_serial's argument arrives in virtual register {SERIAL_VREG:#x}")
    sp = f.inst(f"ptrtoint ptr %serial to i64")
    f.emit(f"store i64 {sp}, ptr {st.reg_at(SERIAL_VREG)}")
    f.emit(f"br label %vip_{entry:x}")

    for vip, block in sorted(blocks.items()):
        f.lines.append("")
        f.label(f"vip_{vip:x}")
        for step_vip, handler, name, _ in block.steps:
            oplen = max(0, lengths.get(handler, 1) - 1)
            raw = S.rd(step_vip + 1, oplen) if oplen else b""
            operand = int.from_bytes(raw, "little") if raw else 0
            width = int("".join(c for c in name if c.isdigit()) or 64)
            f.comment(f"{step_vip:#x}  {name}"
                      + (f" {operand:#x}" if oplen else ""))
            if not emit(st, name.rstrip("?"), width, operand):
                missing.append(name)
                f.comment(f"!! no emitter for {name}")
        # Control flow: one successor becomes a branch, several would need the
        # VIP value the CFG does not resolve yet, so the first is taken.
        succs = sorted(block.successors)
        if f.lines[-1].strip().startswith("ret "):
            pass                       # VM_EXIT already terminated the block
        elif not succs:
            f.comment("no recovered successor: the exploration stopped here")
            f.emit("ret i32 0")
        else:
            # The CFG records *where* control went, never *why*. Emitting an
            # unconditional branch to a back edge would be an infinite loop, so
            # those are cut and the real fix is named: the branch condition
            # lives in the VIP computation, which needs enumerate() on the VIP
            # term plus the flag word the ALU handlers push.
            backs = [x for x in succs if (vip, x) in back_edges]
            if len(succs) > 1:
                f.comment(f"{len(succs)} successors and no recovered condition")
            if backs and len(succs) == 1:
                f.comment(f"back edge to {backs[0]:#x} cut: no loop condition "
                          f"recovered yet, an unconditional branch would hang")
                f.emit("ret i32 0")
            else:
                forward = [x for x in succs if (vip, x) not in back_edges] or succs
                f.emit(f"br label %vip_{forward[0]:x}")

    body = "\n".join(f.lines)
    decls = "\n".join(
        f"declare i32 @dragon.opaque.{k}.{i}() #0"
        for k, n in (("cpuid", 4), ("rdtsc", 2)) for i in range(n))
    return (f"; devirtualized from the recovered VM CFG\n"
            f"define i32 @{f.name}(ptr %serial) {{\n{body}\n}}\n\n"
            f"{decls}\n\nattributes #0 = {{ nounwind memory(none) }}\n"), missing


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--serial", default="AB")
    ap.add_argument("--max-vm", type=int, default=400)
    ap.add_argument("--max-paths", type=int, default=3)
    ap.add_argument("-o", "--out", default="vm_devirt.ll")
    a = ap.parse_args()

    print(f"recovering the CFG from {a.serial!r} ...")
    traces, _forks = C.collect_paths(a.serial, a.max_vm, a.max_paths)
    if not traces:
        print("  nothing explored (VMP_TARGET_DIR?)")
        return
    lengths = C.infer_lengths(traces)
    blocks, back = C.explore(traces)
    print(f"  {len(blocks)} blocks, {len(back)} back edge(s)")

    ir, missing = lift(blocks, lengths, traces[0][0][0], back)
    open(a.out, "w").write(ir)
    print(f"  wrote {a.out}  ({len(ir.splitlines())} lines)")
    if missing:
        seen = sorted(set(missing))
        print(f"  {len(missing)} instruction(s) with no emitter: {', '.join(seen)}")

    opt = llvm_tool("opt")
    if not opt:
        print("  (no `opt` on PATH: skipping verification and optimization)")
        return
    p = subprocess.run([opt, "-passes=verify", "-S", a.out],
                       capture_output=True, text=True)
    if p.returncode:
        print(f"  IR does NOT verify:\n{p.stderr.strip()[:800]}")
        return
    print("  IR verifies.")
    p = subprocess.run([opt, "-O2", "-S", a.out], capture_output=True, text=True)
    if p.returncode == 0:
        out = os.path.splitext(a.out)[0] + ".opt.ll"
        open(out, "w").write(p.stdout)
        body = [l for l in p.stdout.splitlines()
                if l.strip() and not l.startswith((";", "declare", "attributes", "define", "}"))]
        print(f"  wrote {out}: {len(body)} instructions after -O2 "
              f"(from {len(ir.splitlines())} lines raw)")


if __name__ == "__main__":
    main()
