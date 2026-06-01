"""
function_analysis.py

Demonstrates how dragon detects functions and basic blocks in x86_64 code.
Two functions are packed into a single byte buffer. The analyzer discovers
their prologues, delineates their basic blocks, and the example prints a
structured report of each function with its blocks and instructions.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "python"))
import dragon

BASE_ADDRESS = 0x1000

# Two x86_64 functions encoded in a single contiguous buffer.
#
# Function 1 at 0x1000: returns 42 unconditionally.
#
#   push rbp
#   mov  rbp, rsp
#   mov  eax, 42
#   pop  rbp
#   ret
#
# Function 2 at 0x100b: returns 1 if the first argument (edi) is greater
# than zero, otherwise returns -1. Arguments follow the System V AMD64 ABI.
#
#   push rbp
#   mov  rbp, rsp
#   test edi, edi
#   jle  <else>         ; jump if edi <= 0
#   mov  eax, 1
#   pop  rbp
#   ret
# else:
#   mov  eax, -1
#   pop  rbp
#   ret
CODE = bytes([
    # function 1 at 0x1000
    0x55,                           # push rbp
    0x48, 0x89, 0xE5,              # mov rbp, rsp
    0xB8, 0x2A, 0x00, 0x00, 0x00, # mov eax, 42
    0x5D,                          # pop rbp
    0xC3,                          # ret

    # function 2 at 0x100b
    0x55,                           # push rbp
    0x48, 0x89, 0xE5,              # mov rbp, rsp
    0x85, 0xFF,                    # test edi, edi
    0x7E, 0x07,                    # jle +7  (else block at 0x101a)
    0xB8, 0x01, 0x00, 0x00, 0x00, # mov eax, 1
    0x5D,                          # pop rbp
    0xC3,                          # ret
    0xB8, 0xFF, 0xFF, 0xFF, 0xFF, # mov eax, -1
    0x5D,                          # pop rbp
    0xC3,                          # ret
])


def format_insn(insn, indent=6):
    parts = []
    if insn.is_jump:
        parts.append("COND JUMP" if insn.is_conditional else "JUMP")
    if insn.is_call:
        parts.append("CALL")
    if insn.is_return:
        parts.append("RET")
    tag = f"  <{', '.join(parts)}>" if parts else ""
    pad = " " * indent
    return f"{pad}0x{insn.address:08x}  {insn.mnemonic:<8} {insn.operands}{tag}"


def print_function(graph, fn_addr):
    fn = graph.get_function(fn_addr)
    if not fn.is_valid:
        print(f"  Function at 0x{fn_addr:x} is not valid, skipping.")
        return

    print(f"Function 0x{fn.start_address:08x}")
    print(f"  blocks       : {fn.num_blocks}")
    print(f"  instructions : {fn.num_instructions}")
    print(f"  byte size    : {fn.byte_size}")
    print(f"  cyclomatic   : {fn.cyclomatic_complexity}")
    print()

    for block_addr in fn.block_addresses():
        block = graph.get_block(block_addr)
        insns = block.instructions()
        print(f"    block 0x{block_addr:x}  "
              f"({len(insns)} instructions, "
              f"{block.byte_size} bytes, "
              f"terminator at 0x{block.terminator_address:x})")
        for insn in insns:
            print(format_insn(insn))
        successors = block.successors()
        if successors:
            targets = ", ".join(f"0x{s:x}" for s in sorted(successors))
            print(f"      successors: {targets}")
        print()


def main():
    config = dragon.Configuration(syntax=dragon.Syntax.INTEL, logging=True)
    disasm = dragon.Disassembler(dragon.Architecture.X86_64, config)

    graph = disasm.analyze(CODE, BASE_ADDRESS)

    fn_addrs = graph.function_addresses()
    print(f"Discovered {len(fn_addrs)} function(s) "
          f"in {len(CODE)} bytes at 0x{BASE_ADDRESS:x}\n")

    for addr in fn_addrs:
        print_function(graph, addr)


if __name__ == "__main__":
    main()
