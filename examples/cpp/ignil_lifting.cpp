#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>

#include <llvm/MC/MCInst.h>

#include <format>
#include <iostream>
#include <span>
#include <string>
#include <vector>

// -----------------------------------------------------------------------
// Printing
// -----------------------------------------------------------------------

static void printSeparator(char c = '-', int width = 72) {
  std::cout << std::string(width, c) << '\n';
}

static void printComparison(const std::string &title, dragon::Graph &g) {
  printSeparator('=');
  std::cout << "  " << title << '\n';
  printSeparator('=');

  for (const dragon::BasicBlock &block: g.blocks()) {
    std::cout << std::format("\nBlock @ 0x{:x}\n", block.startAddress());
    printSeparator();

    const dragon::ignil::Block *ignilBlock =
            g.getIGNILBlock(block.startAddress());

    for (const dragon::Instruction *insn: block.instructions()) {
      std::cout << std::format("  {:016x}  {}\n", insn->address(),
                               insn->disassembly());

      if (ignilBlock) {
        for (const dragon::ignil::Op &op: *ignilBlock) {
          if (op.address == insn->address())
            std::cout << "                      -> "
                      << dragon::ignil::toString(op) << '\n';
        }
      }
    }
  }
  std::cout << '\n';
}

// -----------------------------------------------------------------------
// Examples
// -----------------------------------------------------------------------

static void exampleArithmetic() {
  // push rbp        55
  // mov rbp, rsp    48 89 E5
  // add rax, rbx    48 01 D8
  // sub rcx, rdx    48 29 D1
  // xor eax, eax    31 C0
  // pop rbp         5D
  // ret             C3
  const std::vector<std::uint8_t> bytes = {
          0x55, // push rbp
          0x48, 0x89, 0xE5, // mov rbp, rsp
          0x48, 0x01, 0xD8, // add rax, rbx
          0x48, 0x29, 0xD1, // sub rcx, rdx
          0x31, 0xC0, // xor eax, eax
          0x5D, // pop rbp
          0xC3, // ret
  };

  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;

  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  dragon::Graph g(dragon::Architecture::X86_64, cfg);
  dis.disassemble(bytes, 0x1000, g);

  printComparison("Arithmetic (push/mov/add/sub/xor/pop/ret)", g);
}

static void exampleLea() {
  // push rbp                  55
  // mov rbp, rsp              48 89 E5
  // lea rax, [rbx + rcx*4 + 8]  48 8D 44 8B 08
  // pop rbp                   5D
  // ret                       C3
  const std::vector<std::uint8_t> bytes = {
          0x55, // push rbp
          0x48, 0x89, 0xE5, // mov rbp, rsp
          0x48, 0x8D, 0x44, 0x8B, 0x08, // lea rax, [rbx + rcx*4 + 8]
          0x5D, // pop rbp
          0xC3, // ret
  };

  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;

  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  dragon::Graph g(dragon::Architecture::X86_64, cfg);
  dis.disassemble(bytes, 0x1000, g);

  printComparison("LEA with SIB addressing: [rbx + rcx*4 + 8]", g);
}

static void exampleConditionalBranch() {
  // 0x1000: 48 39 D8   cmp rax, rbx
  // 0x1003: 7E 05      jle 0x100A
  // 0x1005: 48 01 D8   add rax, rbx   (then-block)
  // 0x1008: C3         ret
  // 0x1009: 90         nop
  // 0x100A: C3         ret            (else-block)
  const std::vector<std::uint8_t> bytes = {
          0x48, 0x39, 0xD8, // cmp rax, rbx
          0x7E, 0x05, // jle 0x100A
          0x48, 0x01, 0xD8, // add rax, rbx
          0xC3, // ret
          0x90, // nop
          0xC3, // ret
  };

  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;

  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  dragon::Graph g(dragon::Architecture::X86_64, cfg);
  dis.disassemble(bytes, 0x1000, g);

  printComparison("Conditional branch: cmp rax, rbx / jle", g);
}

static void exampleIndirectJmpMem() {
  // 0x1000: FF 20   jmp [rax]
  //   Address of the jump target is read from memory at [rax].
  //   IGNIL: emitAddress(rax) → LOAD from that address → JMP to loaded temp.
  // 0x1002: C3      ret  (unreachable, just terminates the block)
  const std::vector<std::uint8_t> bytes = {
          0xFF, 0x20,  // jmp [rax]
          0xC3,        // ret
  };

  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;

  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  dragon::Graph g(dragon::Architecture::X86_64, cfg);
  dis.disassemble(bytes, 0x1000, g);

  printComparison("Indirect jump through memory: jmp [rax]", g);
}

static void exampleIndirectCallMem() {
  // 0x1000: FF 14 CB   call [rbx + rcx*8]
  //   Virtual dispatch / jump-table call.  The callee address is read from the
  //   vtable slot at rbx + rcx*8.
  //   IGNIL: emitAddress(rbx + rcx*8) → LOAD from that address → CALL on loaded temp.
  // 0x1003: C3          ret
  const std::vector<std::uint8_t> bytes = {
          0xFF, 0x14, 0xCB,  // call [rbx + rcx*8]
          0xC3,              // ret
  };

  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;

  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  dragon::Graph g(dragon::Architecture::X86_64, cfg);
  dis.disassemble(bytes, 0x1000, g);

  printComparison("Indirect call through memory: call [rbx + rcx*8]", g);
}

// -----------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------

static void debugDumpMCInst(dragon::Graph &g) {
  printSeparator('=');
  std::cout << "  MCInst raw operand dump\n";
  printSeparator('=');
  for (const dragon::Instruction *insn: g.instructions()) {
    const auto *mc = insn->mcInst();
    if (!mc)
      continue;
    std::cout << std::format("  0x{:x}  {}  opc={}  nops={}\n", insn->address(),
                             insn->disassembly(), mc->getOpcode(),
                             mc->getNumOperands());
    for (unsigned i = 0; i < mc->getNumOperands(); ++i) {
      const auto &op = mc->getOperand(i);
      if (op.isReg())
        std::cout << std::format("    op[{}] = reg {}\n", i, op.getReg());
      else if (op.isImm())
        std::cout << std::format("    op[{}] = imm {}\n", i, op.getImm());
    }
  }
  std::cout << '\n';
}

int main() {
  // Raw MCInst dump to verify operand layout before running lifting examples
  {
    const std::vector<std::uint8_t> bytes = {
            0x48, 0x01, 0xD8, // add rax, rbx
            0x48, 0x29, 0xD1, // sub rcx, rdx
            0x7E, 0x05, // jle +5
            0xC3, // ret
            0x90, 0x90, 0x90, 0x90, 0x90, 0xC3,
    };
    dragon::Configuration cfg;
    cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
    cfg.syntax = dragon::Syntax::INTEL;
    dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
    dragon::Graph g(dragon::Architecture::X86_64, cfg);
    dis.disassemble(bytes, 0x1000, g);
    debugDumpMCInst(g);
  }

  exampleArithmetic();
  exampleLea();
  exampleConditionalBranch();
  exampleIndirectJmpMem();
  exampleIndirectCallMem();
  return 0;
}
