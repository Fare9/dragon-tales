"""
symbolic_execution.py — dragon SymbolicExecutor Python API demo

Executes lifted IGNIL symbolically over Z3 bitvectors and asks the solver
about the result. Four things this is good for:

  1. Reading a block's meaning back out, through the flag-setting noise.
  2. Recovering the input that makes a check pass (the CTF case).
  3. Deciding which edges of a branch are real.
  4. Resolving a computed jump to the concrete addresses it can reach, which
     is what a CFG reconstructor is missing on a virtualised binary.

Every block here is real x86-64 machine code, disassembled and lifted the
same way any other dragon analysis gets one.

Requires the library to have been built with -DDRAGON_USE_Z3=ON.

Run from the repository root:
    python3 examples/python/symbolic_execution.py
"""

import struct
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'python'))

import dragon
from dragon import x86


# ──────────────────────────────────────────────────────────────────────────────
# Helpers
# ──────────────────────────────────────────────────────────────────────────────

def separator(char='─', width=72):
    print(char * width)


def heading(title):
    print()
    separator('═')
    print(title)
    separator('═')


def lift(code: bytes, base: int = 0x1000):
    """Disassemble, lift, and hand back the IGNIL block at `base`."""
    cfg = dragon.Configuration(syntax=dragon.Syntax.INTEL,
                               strategy=dragon.Strategy.LINEAR_SWEEP)
    dis = dragon.Disassembler(dragon.Architecture.X86_64, cfg)
    graph = dis.analyze(code, base)
    block = graph.get_ignil_block(base)
    if block is None:
        raise RuntimeError(f'nothing lifted at 0x{base:x}')
    # The Graph owns the block, so it has to stay alive alongside it.
    return graph, block


# ──────────────────────────────────────────────────────────────────────────────
# 1. Reading a block's meaning back out
# ──────────────────────────────────────────────────────────────────────────────

def demo_expression_recovery():
    heading('1. What does this block actually compute?')

    #   0x1000: 48 31 F8   xor rax, rdi
    #   0x1003: 48 01 F0   add rax, rsi
    #   0x1006: c3         ret
    _graph, block = lift(bytes([0x48, 0x31, 0xF8,
                                0x48, 0x01, 0xF0,
                                0xC3]))

    with dragon.SymbolicExecutor() as ex:
        ex.set_register(x86.RAX, 0)
        a = ex.symbolize_register(x86.RDI, 'a')
        b = ex.symbolize_register(x86.RSI, 'b')

        result = ex.run(block)
        print(f"  {block.size} IGNIL ops, {result.ops_executed} executed, "
              f"exit={result.exit}")

        rax = ex.read_register(x86.RAX)
        # The flags the lifter sets on the way do not get in the way: the
        # solver is asked about the value, not the syntax.
        print(f"  proves RAX == a + b :  {ex.proves(rax == a + b)}")
        print(f"  proves RAX == a * b :  {ex.proves(rax == a * b)}")

        # Pin one argument down and the other is solvable.
        ex.assume(b == ex.const(0x37, 64))
        solution = ex.solve(rax == ex.const(0x1337, 64))
        print(f"  a such that RAX == 0x1337 with b == 0x37:  "
              f"0x{solution.value('a'):x}")


# ──────────────────────────────────────────────────────────────────────────────
# 2. Inverting a check — the CTF case
# ──────────────────────────────────────────────────────────────────────────────

def demo_crackme():
    heading('2. Recovering a flag from an XOR check')

    BUFFER = 0x4000
    KEY = 0x5A5A5A5A5A5A5A5A
    secret = b'DRAGON!!'
    expected = struct.unpack('<Q', secret)[0] ^ KEY

    #   0x1000: 48 8B 07   mov rax, [rdi]
    #   0x1003: 48 31 F0   xor rax, rsi
    #   0x1006: c3         ret
    _graph, block = lift(bytes([0x48, 0x8B, 0x07,
                                0x48, 0x31, 0xF0,
                                0xC3]))

    with dragon.SymbolicExecutor() as ex:
        ex.set_register(x86.RDI, BUFFER)
        ex.set_register(x86.RSI, KEY)
        ex.symbolize_memory(BUFFER, 8, 'flag')
        ex.run(block)

        goal = ex.read_register(x86.RAX) == ex.const(expected, 64)
        solution = ex.solve(goal)

        if solution is None:
            print('  no input satisfies the check')
            return
        print(f"  target    = 0x{expected:016x}")
        print(f"  recovered = {solution.bytes('flag')!r}")
        print(f"  matches   = {solution.bytes('flag') == secret}")

        # A goal with no model is reported as such rather than guessed at.
        rax = ex.read_register(x86.RAX)
        print(f"  unsatisfiable goal returns {ex.solve(rax != rax)!r}")


# ──────────────────────────────────────────────────────────────────────────────
# 3. Deciding a branch
# ──────────────────────────────────────────────────────────────────────────────

def demo_branch_resolution():
    heading('3. Which edges of this branch are real?')

    #   0x1000: 48 83 FF 0A   cmp rdi, 10
    #   0x1004: 7C 03         jl 0x1009
    #   0x1006: 48 FF C0      inc rax
    #   0x1009: c3            ret
    code = bytes([0x48, 0x83, 0xFF, 0x0A,
                  0x7C, 0x03,
                  0x48, 0xFF, 0xC0,
                  0xC3])

    print('  with RDI unknown:')
    _g1, block = lift(code)
    with dragon.SymbolicExecutor() as ex:
        ex.symbolize_register(x86.RDI, 'n')
        result = ex.run(block)
        for s in result.successors:
            print(f"    -> 0x{s.address:x}  (taken={s.taken})")
        print(f"    complete={result.complete}")

    print('  knowing RDI > 100:')
    _g2, block = lift(code)
    with dragon.SymbolicExecutor() as ex:
        n = ex.symbolize_register(x86.RDI, 'n')
        ex.assume(n > ex.const(100, 64))
        result = ex.run(block)
        for s in result.successors:
            print(f"    -> 0x{s.address:x}  (taken={s.taken})")
        print(f"    complete={result.complete}, "
              f"solver queries={result.solver_queries}")


# ──────────────────────────────────────────────────────────────────────────────
# 4. Resolving a dispatch table — the VM case
# ──────────────────────────────────────────────────────────────────────────────

def demo_dispatch_recovery():
    heading('4. Where can this computed jump go?')

    HANDLERS = 0x6000
    BYTECODE = 0x7000
    handlers = [0xAA00, 0xBB00, 0xCC00]
    table = b''.join(struct.pack('<Q', h) for h in handlers)

    # The shape a bytecode VM's dispatcher has: fetch the opcode byte, index
    # the handler table with it, jump there.
    #   0x1000: 0F B6 37                movzx esi, byte ptr [rdi]
    #   0x1003: FF 24 F5 00 60 00 00    jmp qword ptr [rsi*8 + 0x6000]
    code = bytes([0x0F, 0xB6, 0x37,
                  0xFF, 0x24, 0xF5, 0x00, 0x60, 0x00, 0x00])

    print('  with the opcode range-checked:')
    _g1, block = lift(code)
    with dragon.SymbolicExecutor() as ex:
        ex.map_memory(HANDLERS, table)
        ex.set_register(x86.RDI, BYTECODE)

        # A well-formed program only ever holds a valid opcode there.
        opcode = ex.symbolize_memory(BYTECODE, 1, 'vm_opcode')
        ex.assume(opcode.ult(ex.const(len(handlers), 8)))

        result = ex.run(block)
        print(f"    discovered {len(result.successors)} handlers "
              f"(complete={result.complete}):")
        for s in result.successors:
            print(f"      -> 0x{s.address:x}")

    print('  without it, the target is unbounded:')
    _g2, block = lift(code)
    with dragon.SymbolicExecutor() as ex:
        ex.map_memory(HANDLERS, table)
        ex.set_register(x86.RDI, BYTECODE)
        ex.symbolize_memory(BYTECODE, 1, 'vm_opcode')
        result = ex.run(block)
        print(f"    successors={len(result.successors)}, "
              f"complete={result.complete}")
        for w in result.warnings:
            print(f"    warning @0x{w.address:x}: {w.message}")


# ──────────────────────────────────────────────────────────────────────────────
# 5. Proving an obfuscated expression is something simpler
# ──────────────────────────────────────────────────────────────────────────────

def demo_mba():
    heading('5. Seeing through mixed boolean-arithmetic')

    #   rax = (rdi | rsi) + (rdi & rsi), which is just rdi + rsi
    #   0x1000: 48 89 F8   mov rax, rdi
    #   0x1003: 48 09 F0   or  rax, rsi
    #   0x1006: 48 89 F9   mov rcx, rdi
    #   0x1009: 48 21 F1   and rcx, rsi
    #   0x100C: 48 01 C8   add rax, rcx
    #   0x100F: c3         ret
    _g1, block = lift(bytes([0x48, 0x89, 0xF8,
                             0x48, 0x09, 0xF0,
                             0x48, 0x89, 0xF9,
                             0x48, 0x21, 0xF1,
                             0x48, 0x01, 0xC8,
                             0xC3]))

    with dragon.SymbolicExecutor() as ex:
        a = ex.symbolize_register(x86.RDI, 'a')
        b = ex.symbolize_register(x86.RSI, 'b')
        ex.run(block)
        rax = ex.read_register(x86.RAX)
        print(f"  (a|b) + (a&b) == a + b :  {ex.proves(rax == a + b)}")
        print(f"  (a|b) + (a&b) == a ^ b :  {ex.proves(rax == (a ^ b))}")

    # A block whose result does not depend on its input at all:
    #   rcx = ((x ^ k) + k) - (x ^ k)  ==  k, for every x.
    #   0x1000: 48 89 F8   mov rax, rdi
    #   0x1003: 48 31 F0   xor rax, rsi
    #   0x1006: 48 89 C1   mov rcx, rax
    #   0x1009: 48 01 F1   add rcx, rsi
    #   0x100C: 48 29 C1   sub rcx, rax
    #   0x100F: c3         ret
    _g2, block = lift(bytes([0x48, 0x89, 0xF8,
                             0x48, 0x31, 0xF0,
                             0x48, 0x89, 0xC1,
                             0x48, 0x01, 0xF1,
                             0x48, 0x29, 0xC1,
                             0xC3]))

    with dragon.SymbolicExecutor() as ex:
        ex.symbolize_register(x86.RDI, 'x')
        ex.set_register(x86.RSI, 0xA5A5)
        ex.run(block)
        value = ex.evaluate(ex.read_register(x86.RCX))
        print(f"  x-independent result   :  "
              f"{'0x%x' % value if value is not None else 'not constant'}")

        # RAX, by contrast, really does depend on x.
        rax = ex.evaluate(ex.read_register(x86.RAX))
        print(f"  x-dependent result     :  "
              f"{'0x%x' % rax if rax is not None else 'not constant'}")


# ──────────────────────────────────────────────────────────────────────────────

def main():
    demo_expression_recovery()
    demo_crackme()
    demo_branch_resolution()
    demo_dispatch_recovery()
    demo_mba()
    print()


if __name__ == '__main__':
    main()
