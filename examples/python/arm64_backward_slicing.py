"""
arm64_backward_slicing.py — dragon BackwardSlicer Python API demo (AArch64)

Same walkthrough as backward_slicing.py, ported to AArch64. The backward
slicer itself is architecture-independent - only the register namespace
(dragon.arm instead of dragon.x86) and instruction encodings change.

Run from the repository root:
    python3 examples/python/arm64_backward_slicing.py
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
    dis = dragon.Disassembler(dragon.Architecture.ARM64, cfg)
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
#   0x1000:  mov x0, #42   40 05 80 d2
#   0x1004:  ret           c0 03 5f d6
#
# Slicing X0 at the end of the block resolves directly to Const{42}.
# ──────────────────────────────────────────────────────────────────────────────

def example_constant():
    separator('═')
    print("  Example 1 — constant propagation")
    separator('═')

    code = bytes([
        0x40, 0x05, 0x80, 0xd2,  # mov x0, #42
        0xc0, 0x03, 0x5f, 0xd6,  # ret
    ])
    g = make_graph(code, base=0x1000)

    print("IGNIL block:")
    print_ignil_block(g, 0x1000)
    print()
    print_slice(g, 0x1000, 'X0', dragon.arm.X0)
    print()


# ──────────────────────────────────────────────────────────────────────────────
# Example 2 — indirect jump target reconstruction
#
#   0x2000:  mov x0, #0x3000   00 00 86 d2
#   0x2004:  add x0, x0, #8    00 20 00 91
#   0x2008:  br x0             00 00 1f d6
#
# The slicer rebuilds the symbolic expression for the jump target.
# A solver (e.g. Z3) can evaluate it to 0x3008.
# ──────────────────────────────────────────────────────────────────────────────

def example_indirect_jump():
    separator('═')
    print("  Example 2 — indirect jump target")
    separator('═')

    code = bytes([
        0x00, 0x00, 0x86, 0xd2,  # mov x0, #0x3000
        0x00, 0x20, 0x00, 0x91,  # add x0, x0, #8
        0x00, 0x00, 0x1f, 0xd6,  # br x0
    ])
    g = make_graph(code, base=0x2000)

    print("IGNIL block:")
    print_ignil_block(g, 0x2000)
    print()
    print_slice(g, 0x2000, 'X0', dragon.arm.X0)
    print()


# ──────────────────────────────────────────────────────────────────────────────
# Example 3 — cross-block propagation
#
#   Block A  0x3000:  mov x0, #100   ;  b 0x3008
#   Block B  0x3008:  ret
#
# X0 is not defined in Block B so the slicer crosses into Block A
# (the unique predecessor) and resolves it to Const{100}.
# ──────────────────────────────────────────────────────────────────────────────

def example_cross_block():
    separator('═')
    print("  Example 3 — cross-block propagation")
    separator('═')

    code = bytes([
        0x80, 0x0c, 0x80, 0xd2,  # mov x0, #100  @ 0x3000
        0x01, 0x00, 0x00, 0x14,  # b 0x3008      @ 0x3004
        0xc0, 0x03, 0x5f, 0xd6,  # ret           @ 0x3008
    ])
    g = make_graph(code, base=0x3000)

    print("Blocks:")
    for addr in sorted(g.block_addresses()):
        print(f"  Block 0x{addr:x}:")
        print_ignil_block(g, addr)
    print()

    print("Slicing X0 from Block B (0x3008) — crosses into Block A:")
    print_slice(g, 0x3008, 'X0', dragon.arm.X0)
    print()


# ──────────────────────────────────────────────────────────────────────────────
# Example 4 — join node stops propagation
#
#   Block A  0x4000:  cmp x0, x1  /  b.le 0x4010
#   Block B  0x4008:  mov x0, #1  /  b 0x4010  (then-path)
#   Block C  0x4010:  ret           (has two predecessors — join node)
#
# The slicer detects that Block C is a join node (two incoming edges) and
# stops, returning SymReg to signal that X0 is unknown at this point.
# ──────────────────────────────────────────────────────────────────────────────

def example_join_node():
    separator('═')
    print("  Example 4 — join node stops at SymReg")
    separator('═')

    code = bytes([
        0x1f, 0x00, 0x01, 0xeb,  # cmp x0, x1   @ 0x4000
        0x6d, 0x00, 0x00, 0x54,  # b.le 0x4010  @ 0x4004
        0x20, 0x00, 0x80, 0xd2,  # mov x0, #1   @ 0x4008
        0x01, 0x00, 0x00, 0x14,  # b 0x4010     @ 0x400c
        0xc0, 0x03, 0x5f, 0xd6,  # ret          @ 0x4010
    ])
    g = make_graph(code, base=0x4000)

    print("Blocks:")
    for addr in sorted(g.block_addresses()):
        print(f"  Block 0x{addr:x}:")
        print_ignil_block(g, addr)
    print()

    print("Slicing X0 from Block C (0x4010) — two predecessors → join node:")
    print_slice(g, 0x4010, 'X0', dragon.arm.X0)
    print()


# ──────────────────────────────────────────────────────────────────────────────
# Example 5 — slicing a flag (ZF after CMP)
#
#   0x5000:  cmp w0, #5   1f 14 00 71
#   0x5004:  ret          c0 03 5f d6
#
# The lifter models ZF = EQ(w0 - 5, 0). Slicing ZF recovers that
# expression; a conditional-branch analysis can then use it directly.
# ──────────────────────────────────────────────────────────────────────────────

def example_flag_slice():
    separator('═')
    print("  Example 5 — flag slice (ZF after CMP)")
    separator('═')

    code = bytes([
        0x1f, 0x14, 0x00, 0x71,  # cmp w0, #5  @ 0x5000
        0xc0, 0x03, 0x5f, 0xd6,  # ret         @ 0x5004
    ])
    g = make_graph(code, base=0x5000)

    print("IGNIL block:")
    print_ignil_block(g, 0x5000)
    print()
    print_slice(g, 0x5000, 'ZF', dragon.arm.ZF)
    print()


# ──────────────────────────────────────────────────────────────────────────────

if __name__ == '__main__':
    example_constant()
    example_indirect_jump()
    example_cross_block()
    example_join_node()
    example_flag_slice()
