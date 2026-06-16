#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>
#include <dragon/lifters/llvm/LLVMLifter.hpp>

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

static void printComparison(const std::string &title, dragon::Graph &g,
                            dragon::llvmlifter::LLVMLifter &llvml) {
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

  printSeparator('=');
  std::cout << "Non-Optimized Code\n";
  printSeparator('=');

  std::cout << llvml.ir();

  llvml.runMem2Reg();
  llvml.runInstCombine();
  llvml.runGVN();
  llvml.runDCE();

  printSeparator('=');
  std::cout << "Optimized Code\n";
  printSeparator('=');

  std::cout << llvml.ir();
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

  dragon::llvmlifter::LLVMLifter llvml{dragon::Architecture::X86_64,
                                       "ExampleArithmetic"};
  auto funcs = g.functions();
  auto &func = funcs.back();
  if (llvml.lift(func, g))
    printComparison("ExampleArithmetic", g, llvml);
}

// Demonstrates AND, OR, XOR register operations.
// After optimization, only rdi=0 (from xor rdi,rdi) survives DCE;
// and/or results are undef (inputs uninitialized) and are eliminated.
static void exampleBitwiseOps() {
  // push rbp        55
  // mov rbp, rsp    48 89 E5
  // and rax, rbx    48 21 D8
  // or  rcx, rdx    48 09 D1
  // xor rdi, rdi    48 31 FF
  // pop rbp         5D
  // ret             C3
  const std::vector<std::uint8_t> bytes = {
          0x55,                   // push rbp
          0x48, 0x89, 0xE5,       // mov rbp, rsp
          0x48, 0x21, 0xD8,       // and rax, rbx
          0x48, 0x09, 0xD1,       // or  rcx, rdx
          0x48, 0x31, 0xFF,       // xor rdi, rdi
          0x5D,                   // pop rbp
          0xC3,                   // ret
  };

  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;

  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  dragon::Graph g(dragon::Architecture::X86_64, cfg);
  dis.disassemble(bytes, 0x2000, g);

  dragon::llvmlifter::LLVMLifter llvml{dragon::Architecture::X86_64,
                                       "ExampleBitwiseOps"};
  auto funcs = g.functions();
  if (funcs.empty())
    return;
  if (llvml.lift(funcs.front(), g))
    printComparison("ExampleBitwiseOps", g, llvml);
}

// Demonstrates memory load and store via the %mem pointer.
// mov rax, [rbx] -> GEP + load; mov [rcx], rax -> GEP + store.
// The memory write to mem[rcx] is preserved through optimization as a
// visible side effect; the load result is written back to %regs.
static void exampleMemoryOps() {
  // push rbp        55
  // mov rbp, rsp    48 89 E5
  // mov rax, [rbx]  48 8B 03    (load 64-bit from address held in rbx)
  // mov [rcx], rax  48 89 01    (store rax to address held in rcx)
  // pop rbp         5D
  // ret             C3
  const std::vector<std::uint8_t> bytes = {
          0x55,                   // push rbp
          0x48, 0x89, 0xE5,       // mov rbp, rsp
          0x48, 0x8B, 0x03,       // mov rax, [rbx]
          0x48, 0x89, 0x01,       // mov [rcx], rax
          0x5D,                   // pop rbp
          0xC3,                   // ret
  };

  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;

  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  dragon::Graph g(dragon::Architecture::X86_64, cfg);
  dis.disassemble(bytes, 0x3000, g);

  dragon::llvmlifter::LLVMLifter llvml{dragon::Architecture::X86_64,
                                       "ExampleMemoryOps"};
  auto funcs = g.functions();
  if (funcs.empty())
    return;
  if (llvml.lift(funcs.front(), g))
    printComparison("ExampleMemoryOps", g, llvml);
}

// Demonstrates multi-block control flow.
// Lifts an if/else: if (rax == 0) rcx = 1; else rcx = 2;
// Each path ends with its own ret so every basic block has a terminator.
// The non-optimized IR shows three LLVM basic blocks (entry, false, true).
static void exampleConditionalBranch() {
  // Layout (base 0x4000):
  //   0x4000  push rbp          55
  //   0x4001  mov rbp, rsp      48 89 E5
  //   0x4004  test rax, rax     48 85 C0
  //   0x4007  jz 0x4012         74 09        (→ .then; fallthrough → 0x4009)
  //   0x4009  mov rcx, 2        48 C7 C1 02 00 00 00
  //   0x4010  pop rbp           5D
  //   0x4011  ret               C3           [false path]
  //   0x4012  mov rcx, 1        48 C7 C1 01 00 00 00   [.then]
  //   0x4019  pop rbp           5D
  //   0x401A  ret               C3           [true path]
  const std::vector<std::uint8_t> bytes = {
          0x55,                                     // push rbp
          0x48, 0x89, 0xE5,                         // mov rbp, rsp
          0x48, 0x85, 0xC0,                         // test rax, rax
          0x74, 0x09,                               // jz +9 → 0x4012
          0x48, 0xC7, 0xC1, 0x02, 0x00, 0x00, 0x00, // mov rcx, 2
          0x5D,                                     // pop rbp
          0xC3,                                     // ret  (false path)
          0x48, 0xC7, 0xC1, 0x01, 0x00, 0x00, 0x00, // mov rcx, 1  (.then)
          0x5D,                                     // pop rbp
          0xC3,                                     // ret  (true path)
  };

  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;

  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  dragon::Graph g(dragon::Architecture::X86_64, cfg);
  dis.disassemble(bytes, 0x4000, g);

  dragon::llvmlifter::LLVMLifter llvml{dragon::Architecture::X86_64,
                                       "ExampleConditionalBranch"};
  auto funcs = g.functions();
  if (funcs.empty())
    return;
  if (llvml.lift(funcs.front(), g))
    printComparison("ExampleConditionalBranch", g, llvml);
}

// Demonstrates a direct function call.
// call 0x5100 lifts to a call to a declared-but-not-defined fn_5100,
// letting the optimizer see through the call boundary.
static void exampleFunctionCall() {
  // Layout (base 0x5000):
  //   0x5000  push rbp          55
  //   0x5001  mov rbp, rsp      48 89 E5
  //   0x5004  call 0x5100       E8 F7 00 00 00   (offset = 0x5100 - 0x5009)
  //   0x5009  pop rbp           5D
  //   0x500A  ret               C3
  const std::vector<std::uint8_t> bytes = {
          0x55,                               // push rbp
          0x48, 0x89, 0xE5,                   // mov rbp, rsp
          0xE8, 0xF7, 0x00, 0x00, 0x00,       // call 0x5100
          0x5D,                               // pop rbp
          0xC3,                               // ret
  };

  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;

  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  dragon::Graph g(dragon::Architecture::X86_64, cfg);
  dis.disassemble(bytes, 0x5000, g);

  dragon::llvmlifter::LLVMLifter llvml{dragon::Architecture::X86_64,
                                       "ExampleFunctionCall"};
  for (auto &fn : g.functions()) {
    if (fn.startAddress() == 0x5000 && llvml.lift(fn, g)) {
      printComparison("ExampleFunctionCall", g, llvml);
      break;
    }
  }
}

int main() {
  exampleArithmetic();
  exampleBitwiseOps();
  exampleMemoryOps();
  exampleConditionalBranch();
  exampleFunctionCall();
  return 0;
}
