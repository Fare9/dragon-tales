#!/usr/bin/env python3
"""
dt_solve_check_serial.py -- recover a passing serial for check_serial using
dragon-tales symbolic execution + Z3, and validate it against the real binary.

Pipeline: disassemble the (unprotected) check_serial -> IGNIL CFG -> drive the
loop symbolically (unrolled to a fixed length) -> Z3 solves for bytes whose
rolling hash hits 0xEFD327AD.

This works only because of the dragon-tales lifter fix for load-direction
reg-reg (*_REV) encodings; without it the r8d accumulator lifts to UNDEF and
the solver has nothing to constrain. Found via differential testing here.

Run: python3 dt_solve_check_serial.py [length]
"""

from __future__ import annotations

import sys, os
from typing import Optional

# vmp_paths resolves everything environment-specific: where dragon.py lives,
# where the analysed binaries live, and where LLVM's tools are.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vmp_paths import dragon_python_dir, require, target as _target

sys.path.insert(0, dragon_python_dir())
import dragon
from dragon import x86
from pe_report import PE

EXE = _target("serial_check.exe")
CHECK_SERIAL_RVA = 0x7300
TARGET = 0xEFD327AD
# IGNIL block roles (from disassembly)
ENTRY, DASH, BODY, ADV, EXIT = (
    0x140007300,
    0x140007310,
    0x140007314,
    0x140007333,
    0x14000733F,
)
M = 0xFFFFFFFF


def reference_hash(bs: bytes) -> int:
    """The hash `check_serial` computes, written out in Python.

    Kept deliberately independent of anything dragon produces: it is the oracle
    that catches an unfaithful lift, which is exactly how the `*_REV` gap in the
    module docstring was found.
    """
    acc = 0xC0FFEE11
    for ch in bs:
        if ch == 0x2D:  # '-' skipped
            continue
        acc = ((acc << 4) & M) ^ (acc >> 3)
        acc = (acc + ch) & M
        acc ^= 0xDEADBEEF
    return acc & M


def load_blocks() -> tuple[dragon.Graph, dict[int, dragon.IGNILBlock]]:
    """Disassemble `check_serial` and hand back its IGNIL blocks by address.

    Returns the Graph as well as the blocks, and callers must keep it alive:
    the blocks are borrowed views into it, so dropping the graph invalidates
    every block obtained from it.
    """
    pe = PE(require(EXE))
    off = pe.rva_to_off(CHECK_SERIAL_RVA)
    if off is None:
        raise ValueError(f"RVA {CHECK_SERIAL_RVA:#x} is not inside any section")
    code = pe.data[off : off + 0x60]
    dis = dragon.Disassembler(
        dragon.Architecture.X86_64,
        dragon.Configuration(strategy=dragon.Strategy.RECURSIVE_TRAVERSAL),
    )
    g = dis.analyze(code, 0x140007300)
    # blocks are borrowed from g; keep g alive by returning it too
    blocks = {a: g.get_ignil_block(a) for a in g.block_addresses()}
    return g, {a: b for a, b in blocks.items() if b is not None}


def solve(length: int, charset: tuple[int, int] = (0x30, 0x5A)) -> Optional[bytes]:
    """Solve for a `length`-byte serial that hashes to TARGET, or None.

    The loop is unrolled by running the body blocks `length` times, which is
    sound here only because the trip count is the (concrete) serial length.
    `charset` keeps the answer printable; without it the solver is free to
    return control bytes that hash correctly but cannot be typed.
    """
    _g, B = load_blocks()
    ex = dragon.SymbolicExecutor()
    ex.set_register(x86.RCX, 0x4000)
    ex.symbolize_memory(0x4000, length, "serial")
    ex.map_memory(0x4000 + length, b"\x00")  # NUL terminator
    for i in range(length):  # narrow to a charset
        by = ex.read_memory(0x4000 + i, 8)
        ex.assume(by.uge(ex.const(charset[0], 8)))
        ex.assume(by.ule(ex.const(charset[1], 8)))
        ex.assume(by != ex.const(0x2D, 8))  # no '-'
    ex.run(B[ENTRY])  # unroll the loop
    for _ in range(length):
        ex.run(B[DASH])
        ex.run(B[BODY])
        ex.run(B[ADV])
    ex.run(B[EXIT])
    sol = ex.solve(ex.read_register(x86.R8) == ex.const(TARGET, 64))
    return sol.bytes("serial") if sol else None


def main() -> None:
    length = int(sys.argv[1]) if len(sys.argv) > 1 else 8
    # sanity: dragon must compute the same hash as the reference on a concrete input
    _g, B = load_blocks()
    ex = dragon.SymbolicExecutor()
    ex.set_register(x86.RCX, 0x4000)
    ex.map_memory(0x4000, b"A\x00")
    for blk in (ENTRY, DASH, BODY, ADV, EXIT):
        ex.run(B[blk])
    acc = ex.evaluate(ex.read_register(x86.R8))
    assert acc == reference_hash(b"A"), f"lifting unfaithful: {acc:#x}"
    print(f"[sanity] dragon hash('A')={acc:#x} == reference  OK")

    s = solve(length)
    if not s:
        print(f"length {length}: UNSAT")
        return
    ok = reference_hash(s) == TARGET
    print(f"recovered serial: {s.decode('latin1')!r}  (hashes to target: {ok})")
    print("verify against the real binary:")
    print(f"  {EXE} " + repr(s.decode("latin1")))


if __name__ == "__main__":
    main()
