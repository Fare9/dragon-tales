"""
simple_disassembly.py

Demonstrates basic x86_64 disassembly using dragon.
Decodes a short byte sequence and prints the address, mnemonic, operands,
size, and control flow classification of every instruction.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "python"))
import dragon

BASE_ADDRESS = 0x1000

# A small x86_64 function that returns 1 when its first argument (edi) is
# non-zero, and 0 otherwise.
#
#   push rbp
#   mov  rbp, rsp
#   xor  eax, eax      ; default return value: 0
#   test edi, edi      ; check the argument
#   je   skip          ; if zero, skip the next instruction
#   mov  eax, 1        ; non-zero path: return 1
# skip:
#   pop  rbp
#   ret
CODE = bytes([
    0x55,                           # push rbp
    0x48, 0x89, 0xE5,              # mov rbp, rsp
    0x31, 0xC0,                    # xor eax, eax
    0x85, 0xFF,                    # test edi, edi
    0x74, 0x05,                    # je +5 (skip to pop rbp)
    0xB8, 0x01, 0x00, 0x00, 0x00, # mov eax, 1
    0x5D,                          # pop rbp
    0xC3,                          # ret
])


def classify(insn):
    parts = []
    if insn.is_call:
        parts.append("call")
    if insn.is_jump:
        parts.append("conditional jump" if insn.is_conditional else "jump")
    if insn.is_return:
        parts.append("return")
    return " | ".join(parts)


def main():
    config = dragon.Configuration(syntax=dragon.Syntax.INTEL)
    disasm = dragon.Disassembler(dragon.Architecture.X86_64, config)

    graph = disasm.disassemble(CODE, BASE_ADDRESS)

    print(f"Decoded {len(graph)} instructions "
          f"from {len(CODE)} bytes at 0x{BASE_ADDRESS:x}\n")

    for insn in graph.instructions():
        cls = classify(insn)
        cls_str = f"  ({cls})" if cls else ""
        print(f"  0x{insn.address:08x}  {insn.mnemonic:<8} "
              f"{insn.operands:<24} [{insn.size} byte(s)]{cls_str}")
        for target in insn.branches:
            print(f"                branch target: 0x{target:x}")


if __name__ == "__main__":
    main()
