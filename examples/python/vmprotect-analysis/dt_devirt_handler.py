#!/usr/bin/env python3
"""
dt_devirt_handler.py -- devirtualize a VMProtect handler with dragon-tales.

Given a handler VA in serial_check.vmp.exe, lift it to LLVM IR and optimize
(mem2reg/instcombine/gvn/dce) so the junk collapses and the handler's real
micro-op (its effect on VM stack / register file / VIP) falls out.

The IR ABI is sub_<va>(ptr mem, ptr regs): `regs` is the IGNIL register file
(RSP@32, RBP@40, RSI@48, R8@64, R9@72, ...), `mem` is the address space, so
loads/stores off them read as VM-stack / register-file / VIP accesses.

Run: python3 dt_devirt_handler.py [va ...]
"""

from __future__ import annotations

import re
import sys, os
from typing import Optional

# vmp_paths resolves everything environment-specific: where dragon.py lives,
# where the analysed binaries live, and where LLVM's tools are.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vmp_paths import dragon_python_dir, require, target as _target

sys.path.insert(0, dragon_python_dir())
import dragon
from pe_report import PE

EXE = _target("serial_check.vmp.exe")
REGNAME = {
    24: "RBX",
    32: "RSP(regfile)",
    40: "RBP(vm_stack)",
    48: "RSI(vip)",
    64: "R8",
    72: "R9",
    8: "RCX",
    0: "RAX",
    16: "RDX",
    56: "RDI",
}


def devirt(va: int, window: int = 0x200) -> Optional[str]:
    """
    Function to disassemble a PE binary, and obtain the functions,
    from those functions, a lifted IR representation is obtained,
    optimized, and finally the IR is returned. If the function cannot be lifted,
    None is returned.

    A handler is not a function in the binary -- it is reached by the VM's
    `push handler; ret` dispatch -- so recursive-traversal analysis is pointed
    at its address directly and given a byte window to work in.

    :param va: virtual address of the handler.
    :param window: bytes to disassemble from `va`; large enough to cover the
        handler and its internal jumps, small enough not to run into the next.
    :return: optimized LLVM IR, or None if nothing could be lifted (no function
        start recovered, or the lift failed).
    """
    pe = PE(require(EXE))
    base = pe.image_base
    off = pe.rva_to_off(va - base)
    if off is None:
        return None  # `va` is not inside any mapped section
    raw = pe.data[off : off + window]
    dis = dragon.Disassembler(
        dragon.Architecture.X86_64,
        dragon.Configuration(strategy=dragon.Strategy.RECURSIVE_TRAVERSAL),
    )
    g = dis.analyze(raw, va)
    fa = g.function_addresses()
    if not fa:
        return None
    lifter = dragon.LLVMLifter()
    if not lifter.lift(g.get_function(fa[0]), g):
        return None
    lifter.optimize()
    return lifter.ir()


def annotate(ir: str) -> str:
    """Comment each `%regs` offset in `ir` with the VM role it holds.

    VMProtect demotes the native registers to slots in a register file and
    reuses the physical ones for VM state -- RBP as the VM stack pointer, RSI
    as the bytecode pointer, and so on. The lifted IR addresses that file as
    byte offsets off `%regs`, which is unreadable until the offsets are named.
    """
    return "\n".join(repl_line(l) for l in ir.splitlines())


def repl_line(l: str) -> str:
    """One line of `annotate`: append `; ROLE` if the line indexes `%regs`."""
    m = re.search(r"getelementptr i8, ptr %regs, i64 (\d+)", l)
    if m:
        role = REGNAME.get(int(m.group(1)))
        if role:
            return l + f"   ; {role}"
    return l


if __name__ == "__main__":
    vas = [int(x, 16) for x in sys.argv[1:]] or [0x1400E5427]
    for va in vas:
        print(f"\n================ handler {va:#x} ================")
        ir = devirt(va)
        if not ir:
            print("  (could not lift)")
            continue
        # print just the function body (skip module header)
        body = ir.split("{", 1)[1].rsplit("}", 1)[0] if "{" in ir else ir
        print(annotate(body).strip())
