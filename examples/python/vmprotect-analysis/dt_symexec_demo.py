#!/usr/bin/env python3
"""The smallest useful dragon-tales symbolic execution, start to finish.

Assembles three instructions, lifts them to IGNIL, runs them symbolically and
asks Z3 to *prove* what the block computes -- not to test it on inputs, but to
show no input can make it compute anything else.

That proof step is the whole point of a symbolic engine over an emulator: the
same three lines that confirm `rax == rdi + rsi` also confirm the block is *not*
computing `rdi * rsi`, over every possible pair of 64-bit values at once.

Doubles as an install check: if this prints OK, dragon-tales is built with Z3,
the C shim is loadable, and the Python bindings resolve.

Run:      python3 dt_symexec_demo.py
Requires: pip install cffi ; dragon-tales built with -DDRAGON_USE_Z3=ON.
"""

import sys, os

# vmp_paths resolves everything environment-specific: where dragon.py lives,
# where the analysed binaries live, and where LLVM's tools are.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vmp_paths import dragon_python_dir

sys.path.insert(0, dragon_python_dir())
import dragon
from dragon import x86


def main() -> None:
    """Lift `xor rax, rdi ; add rax, rsi ; ret` and prove its effect."""
    print("dragon shim:", dragon._LIB)
    dis = dragon.Disassembler(
        dragon.Architecture.X86_64,
        dragon.Configuration(strategy=dragon.Strategy.LINEAR_SWEEP),
    )
    # xor rax,rdi ; add rax,rsi ; ret   -> rax = rdi + rsi
    g = dis.analyze(bytes([0x48, 0x31, 0xF8, 0x48, 0x01, 0xF0, 0xC3]), 0x1000)
    # The IGNIL block is *borrowed* from the graph: it stays valid only while
    # `g` is alive. Blocks from Disassembler.lift_trace() are owned instead.
    blk = g.get_ignil_block(0x1000)
    with dragon.SymbolicExecutor() as ex:
        ex.set_register(x86.RAX, 0)
        # Naming the symbols is what makes the recovered expressions readable
        # later; the name travels all the way into Z3's output.
        a = ex.symbolize_register(x86.RDI, "a")
        b = ex.symbolize_register(x86.RSI, "b")
        ex.run(blk)
        # `==` on a SymExpr builds an expression, it does not compare -- the
        # same convention z3py uses. proves() is what returns a verdict.
        rax = ex.read_register(x86.RAX)
        assert ex.proves(rax == a + b), "expected rax == a+b"
        # ...and the engine will not prove something merely because it was
        # asked: this one has counterexamples, so it must come back false.
        assert not ex.proves(rax == a * b)
        print("OK: dragon-tales symbolic execution + Z3 proves rax == rdi + rsi")


if __name__ == "__main__":
    main()
