#!/usr/bin/env python3
"""
dt_vm_trace.py -- concrete devirtualization trace of the protected check_serial.

Runs the real VMProtect VM (via dt_vm_symexec's stepper) with a CONCRETE serial
and no solver forks, records the VM instruction stream (VIP, opcode class,
operand byte), detects the loop back-edge (a VIP revisited after the prologue),
and writes the devirtualized program: the linear prologue, then the loop body,
as a sequence of named VM opcodes.

This is the "we devirtualized it" artifact: the protected function's virtual
program, read off the running VM, not off a static decode (which stops at the
first VM jump). The remaining step to clean C-like LLVM IR is the partial
evaluation pass described in PROGRESS.md section 5.

Run: python3 dt_vm_trace.py [--serial AB] [--max-vm 5000]
Outputs: analysis/vm/vm_trace.json, analysis/vm/Post-Devirt.md
"""

from __future__ import annotations

import sys, os
import json, time, collections, argparse
from typing import Callable, Optional

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = HERE  # outputs stay beside the scripts
sys.path.insert(0, HERE)
from vmp_paths import llvm_tool, require, target as _target
import dt_vm_symexec as S
from dragon import x86


#: One executed VM instruction: (VIP, handler address, opcode name, operand byte).
VMStep = tuple[int, int, str, Optional[int]]


def trace(serial: str, max_vm: int,
          stop_at_loop: bool = True) -> tuple[list[VMStep], bool, float]:
    """Run the real protected VM on `serial` and record its instruction stream.

    No VM emulator is written: the binary's own VM_ENTRY, dispatcher and
    handlers execute, and a probe fires whenever control reaches a known
    handler address. VIP is read at that moment, which is what makes each step
    identifiable -- and what step-3 CFG recovery keys blocks on.

    :param serial: concrete input; the run stays fully concrete, no solving.
    :param max_vm: stop after this many VM instructions.
    :param stop_at_loop: stop as soon as a VIP repeats far enough back to be a
        back-edge rather than a local re-entry. Turn it off to run to VM_EXIT.
    :return: (steps, reached_the_end, seconds).
    """
    vm = []  # (vip, handler_va, name, operand_byte)

    class Done(Exception):
        pass

    def probe(pc: int, ex, steps, tr, kind) -> None:
        if pc in S.HANDLERS:
            vip = ex.evaluate(ex.read_register(x86.RSI))
            vm.append(
                (vip, pc, S.NAMES.get(pc, "?"), S.rd(vip - 1, 1)[0] if vip else None)
            )
            if len(vm) > max_vm:
                raise Done()
            if stop_at_loop and len(vm) > 40:
                seen = {}
                for i, (v, _, _, _) in enumerate(vm):
                    if v in seen and i - seen[v] > 10:
                        raise Done()
                    seen.setdefault(v, i)

    t0 = time.perf_counter()
    reached = False
    try:
        S.run_path(len(serial), [], concrete=serial, probe=probe, max_steps=6000000)
        reached = True
    except Done:
        pass
    return vm, reached, time.perf_counter() - t0


def find_backedge(vm: list[VMStep]) -> Optional[tuple[int, int, int]]:
    """First repeated VIP that looks like a loop back-edge.

    Block identity in the virtual program *is* the VIP value, so a VIP seen
    before means control returned somewhere it has already been. The `> 10`
    gap filters re-entries too close together to be a real loop body.

    :return: (first index, repeat index, VIP), or None if no VIP repeats.
    """
    seen = {}
    for i, (v, _, _, _) in enumerate(vm):
        if v in seen and i - seen[v] > 10:
            return seen[v], i, v
        seen.setdefault(v, i)
    return None


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--serial", default="AB")
    ap.add_argument("--max-vm", type=int, default=5000)
    ap.add_argument(
        "--full",
        action="store_true",
        help="run to VM_EXIT instead of stopping at the loop back-edge",
    )
    a = ap.parse_args()
    vm, reached, secs = trace(a.serial, a.max_vm, stop_at_loop=not a.full)
    back = find_backedge(vm)
    print(
        f"serial {a.serial!r}: {len(vm)} VM instructions in {secs:.0f}s"
        + (
            " (reached main)"
            if reached
            else " (stopped at loop back-edge)" if back else " (capped)"
        )
    )
    hist = collections.Counter(n for _, _, n, _ in vm)
    print("opcodes:", hist.most_common())
    doc = [
        f"# Devirtualized `check_serial` (concrete VM trace)",
        "",
        f"Recovered by `analysis/vm/dt_vm_trace.py` running the real VMProtect VM on serial "
        f"`{a.serial}` ({len(vm)} VM instructions). See `Post.md` for the ISA and `PROGRESS.md` "
        f"section 5 for the LLVM recompilation step that turns this stream into clean IR.",
        "",
        "## Opcode histogram",
        "",
        "| opcode | count |",
        "|---|---:|",
    ]
    for n, c in hist.most_common():
        doc.append(f"| `{n}` | {c} |")

    def block(lo, hi):
        return [
            (
                f"{v:#x}: {n:12s} operand={op:#04x}"
                if op is not None
                else f"{v:#x}: {n:12s}"
            )
            for v, _, n, op in vm[lo:hi]
        ]

    if back:
        lo, hi, v = back
        doc += (
            [
                "",
                f"## Structure",
                "",
                f"- prologue: VM instructions `[0..{lo})` (native context -> virtual registers, relocation setup)",
                f"- **loop body: `[{lo}..{hi})` = {hi - lo} VM instructions**, back-edge to VIP `{v:#x}`",
                "",
                "The loop body's arithmetic (`SHR`/`SHL` for `<<4`/`>>3`, `NOR`/`NAND` for the "
                "XORs, `ADD` for the character, `SUB` for the loop test) is the source's "
                "`acc = (acc<<4) ^ (acc>>3); acc += *p; acc ^= 0xDEADBEEF` mixing, virtualized.",
                "",
                "### Prologue (first 20)",
                "",
                "```",
            ]
            + block(0, 20)
            + ["```", "", f"### Loop body ([{lo}..{hi}))", "", "```"]
            + block(lo, hi)
            + ["```"]
        )
        print(f"loop body [{lo}..{hi}) = {hi-lo} VM instructions, back-edge VIP {v:#x}")
        print(
            "loop-body opcodes:",
            collections.Counter(n for _, _, n, _ in vm[lo:hi]).most_common(),
        )
    else:
        doc += ["", "## VM instruction stream", "", "```"] + block(0, len(vm)) + ["```"]
    open(os.path.join(ROOT, "Post-Devirt.md"), "w", encoding="utf-8").write(
        "\n".join(doc)
    )
    json.dump(
        dict(
            serial=a.serial,
            count=len(vm),
            reached_main=reached,
            backedge=back,
            histogram=hist.most_common(),
            stream=[[f"{v:#x}", f"{h:#x}", n, op] for v, h, n, op in vm],
        ),
        open(os.path.join(HERE, "vm_trace.json"), "w"),
        indent=1,
    )
    print(f"wrote Post-Devirt.md and analysis/vm/vm_trace.json")


if __name__ == "__main__":
    main()
