"""
backward_slicing.py — dragon BackwardSlicer Python API demo

Shows how to use the BackwardSlicer to trace register values backwards
through lifted IGNIL blocks and reconstruct symbolic expressions.

Run from the repository root:
    python3 examples/python/backward_slicing.py
"""

import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'python'))

import dragon


# ──────────────────────────────────────────────────────────────────────────────
# Helpers
# ──────────────────────────────────────────────────────────────────────────────

def separator(char='─', width=72):
    print(char * width)


def make_graph(code: bytes, base: int = 0x1000) -> dragon.Graph:
    cfg = dragon.Configuration(syntax=dragon.Syntax.INTEL,
                                strategy=dragon.Strategy.LINEAR_SWEEP)
    dis = dragon.Disassembler(dragon.Architecture.X86_64, cfg)
    return dis.analyze(code, base)


def print_ignil_block(graph: dragon.Graph, addr: int):
    block = graph.get_ignil_block(addr)
    if block is None:
        print(f"  (no IGNIL block at 0x{addr:x})")
        return
    print(block.to_string(), end='')


def print_slice(graph: dragon.Graph, block_addr: int, reg_name: str, reg: tuple):
    with dragon.BackwardSlicer(max_blocks=5) as slicer:
        with slicer.slice_reg(graph, block_addr, *reg) as expr:
            print(f"  slice({reg_name}) @ 0x{block_addr:x}  =>  {expr.to_string()}")
            _print_expr_tree(expr, indent=4)


def _print_expr_tree(expr: dragon.SliceExpr, indent: int = 0):
    pad = ' ' * indent
    t = expr.type
    if t == dragon.SliceExprType.CONST:
        print(f"{pad}Const  value=0x{expr.const_value:x}  width={expr.const_width}")
    elif t == dragon.SliceExprType.SYM_REG:
        print(f"{pad}SymReg  offset={expr.symreg_offset}  width={expr.symreg_width}"
              f"  (unknown input)")
    elif t == dragon.SliceExprType.BINOP:
        print(f"{pad}BinOp  op={expr.opcode_name}")
        with expr.left() as l:
            _print_expr_tree(l, indent + 4)
        with expr.right() as r:
            _print_expr_tree(r, indent + 4)
    elif t == dragon.SliceExprType.UNARYOP:
        print(f"{pad}UnaryOp  op={expr.opcode_name}")
        with expr.operand() as o:
            _print_expr_tree(o, indent + 4)
    elif t == dragon.SliceExprType.LOAD:
        print(f"{pad}Load  width={expr.load_width}")
        with expr.load_addr() as a:
            _print_expr_tree(a, indent + 4)
    else:
        print(f"{pad}Undef")


# ──────────────────────────────────────────────────────────────────────────────
# Example 1 — constant propagation in a single block
#
#   0x1000:  mov rax, 42   48 C7 C0 2A 00 00 00
#   0x1007:  ret           C3
#
# Slicing RAX at the end of the block resolves directly to Const{42}.
# ──────────────────────────────────────────────────────────────────────────────

def example_constant():
    separator('═')
    print("  Example 1 — constant propagation")
    separator('═')

    code = bytes([
        0x48, 0xC7, 0xC0, 0x2A, 0x00, 0x00, 0x00,  # mov rax, 42
        0xC3,                                         # ret
    ])
    g = make_graph(code, base=0x1000)

    print("IGNIL block:")
    print_ignil_block(g, 0x1000)
    print()
    print_slice(g, 0x1000, 'RAX', dragon.x86.RAX)
    print()


# ──────────────────────────────────────────────────────────────────────────────
# Example 2 — indirect jump target reconstruction
#
#   0x2000:  mov rax, 0x3000   48 C7 C0 00 30 00 00
#   0x2007:  add rax, 8        48 83 C0 08
#   0x200B:  jmp rax           FF E0
#
# The slicer rebuilds the symbolic expression for the jump target.
# A solver (e.g. Z3) can evaluate it to 0x3008.
# ──────────────────────────────────────────────────────────────────────────────

def example_indirect_jump():
    separator('═')
    print("  Example 2 — indirect jump target")
    separator('═')

    code = bytes([
        0x48, 0xC7, 0xC0, 0x00, 0x30, 0x00, 0x00,  # mov rax, 0x3000
        0x48, 0x83, 0xC0, 0x08,                     # add rax, 8
        0xFF, 0xE0,                                  # jmp rax
    ])
    g = make_graph(code, base=0x2000)

    print("IGNIL block:")
    print_ignil_block(g, 0x2000)
    print()
    print_slice(g, 0x2000, 'RAX', dragon.x86.RAX)
    print()


# ──────────────────────────────────────────────────────────────────────────────
# Example 3 — cross-block propagation
#
#   Block A  0x3000:  mov rax, 100   ;  jmp 0x3009
#   Block B  0x3009:  ret
#
# RAX is not defined in Block B so the slicer crosses into Block A
# (the unique predecessor) and resolves it to Const{100}.
# ──────────────────────────────────────────────────────────────────────────────

def example_cross_block():
    separator('═')
    print("  Example 3 — cross-block propagation")
    separator('═')

    code = bytes([
        0x48, 0xC7, 0xC0, 0x64, 0x00, 0x00, 0x00,  # mov rax, 100  @ 0x3000
        0xEB, 0x00,                                  # jmp 0x3009    @ 0x3007
        0xC3,                                        # ret           @ 0x3009
    ])
    g = make_graph(code, base=0x3000)

    print("Blocks:")
    for addr in sorted(g.block_addresses()):
        print(f"  Block 0x{addr:x}:")
        print_ignil_block(g, addr)
    print()

    print("Slicing RAX from Block B (0x3009) — crosses into Block A:")
    print_slice(g, 0x3009, 'RAX', dragon.x86.RAX)
    print()


# ──────────────────────────────────────────────────────────────────────────────
# Example 4 — join node stops propagation
#
#   Block A  0x4000:  cmp rax, rbx  /  jle 0x400C
#   Block B  0x4005:  mov rax, 1    (then-path, falls through to 0x400C)
#   Block C  0x400C:  ret           (has two predecessors — join node)
#
# The slicer detects that Block C is a join node (two incoming edges) and
# stops, returning SymReg to signal that RAX is unknown at this point.
# ──────────────────────────────────────────────────────────────────────────────

def example_join_node():
    separator('═')
    print("  Example 4 — join node stops at SymReg")
    separator('═')

    code = bytes([
        0x48, 0x39, 0xD8,                           # cmp rax, rbx   @ 0x4000
        0x7E, 0x07,                                  # jle 0x400C     @ 0x4003
        0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,  # mov rax, 1     @ 0x4005
        0xC3,                                        # ret            @ 0x400C
    ])
    g = make_graph(code, base=0x4000)

    print("Blocks:")
    for addr in sorted(g.block_addresses()):
        print(f"  Block 0x{addr:x}:")
        print_ignil_block(g, addr)
    print()

    print("Slicing RAX from Block C (0x400C) — two predecessors → join node:")
    print_slice(g, 0x400C, 'RAX', dragon.x86.RAX)
    print()


# ──────────────────────────────────────────────────────────────────────────────
# Example 5 — slicing a flag (ZF after CMP)
#
#   0x5000:  cmp rax, rbx   48 39 D8
#   0x5003:  ret            C3
#
# The lifter models ZF = EQ(rax - rbx, 0).  Slicing ZF recovers that
# expression; a conditional-branch analysis can then use it directly.
# ──────────────────────────────────────────────────────────────────────────────

def example_flag_slice():
    separator('═')
    print("  Example 5 — flag slice (ZF after CMP)")
    separator('═')

    code = bytes([
        0x48, 0x39, 0xD8,  # cmp rax, rbx  @ 0x5000
        0xC3,              # ret           @ 0x5003
    ])
    g = make_graph(code, base=0x5000)

    print("IGNIL block:")
    print_ignil_block(g, 0x5000)
    print()
    print_slice(g, 0x5000, 'ZF', dragon.x86.ZF)
    print()


# ──────────────────────────────────────────────────────────────────────────────

if __name__ == '__main__':
    example_constant()
    example_indirect_jump()
    example_cross_block()
    example_join_node()
    example_flag_slice()
