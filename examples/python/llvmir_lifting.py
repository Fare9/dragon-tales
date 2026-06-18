"""
llvmir_lifting.py — dragon LLVMLifter Python API demo

Mirrors examples/cpp/llvmir_lifting.cpp.  Each example disassembles a short
x86-64 byte sequence, lifts it to LLVM IR, and prints both the raw and
optimized forms alongside the original disassembly and IGNIL ops.

Run from the repository root:
    python3 examples/python/llvmir_lifting.py
"""

import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'python'))

import dragon


# ==============================================================================
# Helpers
# ==============================================================================

def separator(char='=', width=72):
    print(char * width)


def make_graph(code: bytes, base: int) -> dragon.Graph:
    cfg = dragon.Configuration(syntax=dragon.Syntax.INTEL,
                               strategy=dragon.Strategy.LINEAR_SWEEP)
    dis = dragon.Disassembler(dragon.Architecture.X86_64, cfg)
    return dis.analyze(code, base)


def print_comparison(title: str, graph: dragon.Graph, lifter: dragon.LLVMLifter):
    separator('═')
    print(f"  {title}")
    separator('═')

    for addr in sorted(graph.block_addresses()):
        print(f"\nBlock @ 0x{addr:x}")
        separator()

        block     = graph.get_block(addr)
        ignil_blk = graph.get_ignil_block(addr)

        ignil_ops_by_addr: dict[int, list[str]] = {}
        if ignil_blk:
            for op_addr, _, idx in ignil_blk.ops():
                ignil_ops_by_addr.setdefault(op_addr, []).append(
                    ignil_blk.op_to_string(idx))

        for insn in block.instructions():
            print(f"  {insn.address:016x}  {insn.disassembly}")
            for op_str in ignil_ops_by_addr.get(insn.address, []):
                print(f"                      -> {op_str}")

    separator('═')
    print("Non-Optimized Code")
    separator('═')
    print(lifter.ir())

    lifter.optimize()

    separator('═')
    print("Optimized Code")
    separator('═')
    print(lifter.ir())


# ==============================================================================
# Example 1 — arithmetic
#
#   push rbp        55
#   mov  rbp, rsp   48 89 E5
#   add  rax, rbx   48 01 D8
#   sub  rcx, rdx   48 29 D1
#   xor  eax, eax   31 C0
#   pop  rbp        5D
#   ret             C3
# ==============================================================================

def example_arithmetic():
    code = bytes([
        0x55,                    # push rbp
        0x48, 0x89, 0xE5,        # mov  rbp, rsp
        0x48, 0x01, 0xD8,        # add  rax, rbx
        0x48, 0x29, 0xD1,        # sub  rcx, rdx
        0x31, 0xC0,              # xor  eax, eax
        0x5D,                    # pop  rbp
        0xC3,                    # ret
    ])
    graph  = make_graph(code, base=0x1000)
    lifter = dragon.LLVMLifter(dragon.Architecture.X86_64, 'ExampleArithmetic')
    fn     = graph.get_function(0x1000)
    if lifter.lift(fn, graph):
        print_comparison('ExampleArithmetic', graph, lifter)


# ==============================================================================
# Example 2 — bitwise operations
#
#   push rbp        55
#   mov  rbp, rsp   48 89 E5
#   and  rax, rbx   48 21 D8
#   or   rcx, rdx   48 09 D1
#   xor  rdi, rdi   48 31 FF
#   pop  rbp        5D
#   ret             C3
# ==============================================================================

def example_bitwise_ops():
    code = bytes([
        0x55,                    # push rbp
        0x48, 0x89, 0xE5,        # mov  rbp, rsp
        0x48, 0x21, 0xD8,        # and  rax, rbx
        0x48, 0x09, 0xD1,        # or   rcx, rdx
        0x48, 0x31, 0xFF,        # xor  rdi, rdi
        0x5D,                    # pop  rbp
        0xC3,                    # ret
    ])
    graph  = make_graph(code, base=0x2000)
    lifter = dragon.LLVMLifter(dragon.Architecture.X86_64, 'ExampleBitwiseOps')
    fn     = graph.get_function(0x2000)
    if lifter.lift(fn, graph):
        print_comparison('ExampleBitwiseOps', graph, lifter)


# ==============================================================================
# Example 3 — memory operations
#
#   push rbp          55
#   mov  rbp, rsp     48 89 E5
#   mov  rax, [rbx]   48 8B 03   (load 64-bit from address in rbx)
#   mov  [rcx], rax   48 89 01   (store rax to address in rcx)
#   pop  rbp          5D
#   ret               C3
# ==============================================================================

def example_memory_ops():
    code = bytes([
        0x55,                    # push rbp
        0x48, 0x89, 0xE5,        # mov  rbp, rsp
        0x48, 0x8B, 0x03,        # mov  rax, [rbx]
        0x48, 0x89, 0x01,        # mov  [rcx], rax
        0x5D,                    # pop  rbp
        0xC3,                    # ret
    ])
    graph  = make_graph(code, base=0x3000)
    lifter = dragon.LLVMLifter(dragon.Architecture.X86_64, 'ExampleMemoryOps')
    fn     = graph.get_function(0x3000)
    if lifter.lift(fn, graph):
        print_comparison('ExampleMemoryOps', graph, lifter)


# ==============================================================================
# Example 4 — conditional branch  (if rax == 0: rcx = 1  else: rcx = 2)
#
#   0x4000  push rbp            55
#   0x4001  mov  rbp, rsp       48 89 E5
#   0x4004  test rax, rax       48 85 C0
#   0x4007  jz   0x4012         74 09
#   0x4009  mov  rcx, 2         48 C7 C1 02 00 00 00   (false path)
#   0x4010  pop  rbp            5D
#   0x4011  ret                 C3
#   0x4012  mov  rcx, 1         48 C7 C1 01 00 00 00   (true path)
#   0x4019  pop  rbp            5D
#   0x401A  ret                 C3
# ==============================================================================

def example_conditional_branch():
    code = bytes([
        0x55,                                      # push rbp
        0x48, 0x89, 0xE5,                          # mov  rbp, rsp
        0x48, 0x85, 0xC0,                          # test rax, rax
        0x74, 0x09,                                # jz   0x4012
        0x48, 0xC7, 0xC1, 0x02, 0x00, 0x00, 0x00, # mov  rcx, 2
        0x5D,                                      # pop  rbp
        0xC3,                                      # ret  (false path)
        0x48, 0xC7, 0xC1, 0x01, 0x00, 0x00, 0x00, # mov  rcx, 1
        0x5D,                                      # pop  rbp
        0xC3,                                      # ret  (true path)
    ])
    graph  = make_graph(code, base=0x4000)
    lifter = dragon.LLVMLifter(dragon.Architecture.X86_64, 'ExampleConditionalBranch')
    fn     = graph.get_function(0x4000)
    if lifter.lift(fn, graph):
        print_comparison('ExampleConditionalBranch', graph, lifter)


# ==============================================================================
# Example 5 — direct function call
#
#   0x5000  push rbp        55
#   0x5001  mov  rbp, rsp   48 89 E5
#   0x5004  call 0x5100     E8 F7 00 00 00
#   0x5009  pop  rbp        5D
#   0x500A  ret             C3
# ==============================================================================

def example_function_call():
    code = bytes([
        0x55,                          # push rbp
        0x48, 0x89, 0xE5,              # mov  rbp, rsp
        0xE8, 0xF7, 0x00, 0x00, 0x00, # call 0x5100
        0x5D,                          # pop  rbp
        0xC3,                          # ret
    ])
    graph  = make_graph(code, base=0x5000)
    lifter = dragon.LLVMLifter(dragon.Architecture.X86_64, 'ExampleFunctionCall')
    fn     = graph.get_function(0x5000)
    if lifter.lift(fn, graph):
        print_comparison('ExampleFunctionCall', graph, lifter)


# ==============================================================================

if __name__ == '__main__':
    example_arithmetic()
    example_bitwise_ops()
    example_memory_ops()
    example_conditional_branch()
    example_function_call()
