#include <dragon/analysis/BackwardSlicer.hpp>
#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>
#include <dragon/analysis/SliceExpr.hpp>
#include <dragon/lifters/ignil/x86/x86IGNILRegs.hpp>

#include <cstdint>
#include <format>
#include <iostream>
#include <string>
#include <vector>

namespace x86 = dragon::x86;

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static void printSeparator(char c = '-', int width = 72) {
  std::cout << std::string(width, c) << '\n';
}

static dragon::Graph disassemble(const std::vector<std::uint8_t> &bytes,
                                 std::uint64_t base) {
  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;

  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  dragon::Graph g(dragon::Architecture::X86_64, cfg);
  dis.disassemble(bytes, base, g);
  return g;
}

// Print every instruction in a block side-by-side with its IGNIL ops.
static void printBlockWithIgnil(dragon::Graph &g,
                                const dragon::BasicBlock &block) {
  const dragon::ignil::Block *ib = g.getIGNILBlock(block.startAddress());
  std::cout << std::format("  Block @ 0x{:x}\n", block.startAddress());

  for (const dragon::Instruction *insn: block.instructions()) {
    std::cout << std::format("    {:08x}  {}\n", insn->address(),
                             insn->disassembly());
    if (ib) {
      for (const dragon::ignil::Op &op: *ib) {
        if (op.address == insn->address())
          std::cout << "              -> " << dragon::ignil::toString(op)
                    << '\n';
      }
    }
  }
}

static void printAllBlocks(dragon::Graph &g) {
  for (const dragon::BasicBlock &b: g.blocks())
    printBlockWithIgnil(g, b);
}

static void printSliceResult(const std::string &label,
                             const dragon::ignil::Reg &reg,
                             std::uint64_t blockAddr, dragon::Graph &g) {
  dragon::BackwardSlicer slicer{5};
  dragon::slice::Expr result =
          slicer.slice(dragon::ignil::Value{reg}, blockAddr, g);

  std::cout << std::format("  slice({}) @ block 0x{:x}  ==>  {}\n", label,
                           blockAddr, dragon::slice::toString(result));
}

// -----------------------------------------------------------------------
// Example 1 — constant folding through a single block
//
//   0x1000:  mov rax, 42         48 C7 C0 2A 00 00 00
//   0x1007:  ret                 C3
//
// Slicing RAX at the end of the single block should resolve to Const{42}.
// -----------------------------------------------------------------------

static void exampleConstant() {
  printSeparator('=');
  std::cout << "  Example 1 — constant folding\n";
  printSeparator('=');

  // mov rax, 42  ;  ret
  const std::vector<std::uint8_t> bytes = {
          0x48, 0xC7, 0xC0, 0x2A, 0x00, 0x00, 0x00, // mov rax, 42
          0xC3, // ret
  };
  auto g = disassemble(bytes, 0x1000);

  printAllBlocks(g);
  std::cout << '\n';
  printSliceResult("RAX", x86::RAX, 0x1000, g);
  std::cout << '\n';
}

// -----------------------------------------------------------------------
// Example 2 — indirect jump target resolution
//
//   0x2000:  mov rax, 0x3000     48 C7 C0 00 30 00 00
//   0x2007:  add rax, 8          48 83 C0 08
//   0x200B:  jmp rax             FF E0
//
// Slicing RAX before the indirect JMP shows the full symbolic expression
// for the jump target.  From this, a solver like Z3 could compute the
// concrete destination.
// -----------------------------------------------------------------------

static void exampleIndirectJump() {
  printSeparator('=');
  std::cout << "  Example 2 — indirect jump target\n";
  printSeparator('=');

  const std::vector<std::uint8_t> bytes = {
          0x48, 0xC7, 0xC0, 0x00, 0x30, 0x00, 0x00, // mov rax, 0x3000
          0x48, 0x83, 0xC0, 0x08, // add rax, 8
          0xFF, 0xE0, // jmp rax
  };
  auto g = disassemble(bytes, 0x2000);

  printAllBlocks(g);
  std::cout << '\n';
  printSliceResult("RAX", x86::RAX, 0x2000, g);
  std::cout << '\n';
}

// -----------------------------------------------------------------------
// Example 3 — cross-block propagation
//
//   Block A  0x3000:
//     mov rax, 100    48 C7 C0 64 00 00 00
//     jmp 0x3009      EB 00            (rel8=0 → target 0x3009)
//
//   Block B  0x3009:
//     ret             C3
//
// Block B has a single predecessor (A), so the slicer crosses into A and
// resolves RAX to Const{100}.
// -----------------------------------------------------------------------

static void exampleCrossBlock() {
  printSeparator('=');
  std::cout << "  Example 3 — cross-block propagation\n";
  printSeparator('=');

  const std::vector<std::uint8_t> bytes = {
          0x48, 0xC7, 0xC0, 0x64, 0x00, 0x00, 0x00, // mov rax, 100  @ 0x3000
          0xEB, 0x00, // jmp 0x3009    @ 0x3007
          0xC3, // ret           @ 0x3009
  };
  auto g = disassemble(bytes, 0x3000);

  printAllBlocks(g);
  std::cout << '\n';
  // Slice RAX from within block B (0x3009) — should cross into A.
  printSliceResult("RAX", x86::RAX, 0x3009, g);
  std::cout << '\n';
}

// -----------------------------------------------------------------------
// Example 4 — join node stops propagation
//
//   Block A  0x4000:  cmp rax, rbx  /  jle 0x4012
//   Block B  0x4005:  mov rax, 1    /  jmp 0x4012
//   Block C  0x4012:  ret
//
// C has two predecessors (A falls through to B, and both A via jle and B
// via jmp flow into C).  The slicer stops at the join and returns SymReg.
// -----------------------------------------------------------------------

static void exampleJoinNode() {
  printSeparator('=');
  std::cout << "  Example 4 — join node stops at SymReg\n";
  printSeparator('=');

  // Layout (all addresses relative to base 0x4000):
  //   0x4000  48 39 D8              cmp rax, rbx          (3)
  //   0x4003  7E 07                 jle 0x400C            (2) taken=0x400C
  //   fall=0x4005 0x4005  48 C7 C0 01 00 00 00  mov rax, 1            (7)
  //   0x400C  C3                    ret
  const std::vector<std::uint8_t> bytes = {
          0x48, 0x39, 0xD8, // cmp rax, rbx   @ 0x4000
          0x7E, 0x07, // jle 0x400C     @ 0x4003 (offset=7 from 0x4005)
          0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, // mov rax, 1     @ 0x4005
          0xC3, // ret            @ 0x400C
  };
  auto g = disassemble(bytes, 0x4000);

  printAllBlocks(g);
  std::cout << '\n';

  // Blocks and their addresses
  std::cout << "  Block addresses in graph:\n";
  for (const dragon::BasicBlock &b: g.blocks())
    std::cout << std::format("    0x{:x}\n", b.startAddress());
  std::cout << '\n';

  // Slice RAX from the ret block (0x400C):
  //   0x400C has predecessors: 0x4000 (jle taken) and 0x4005 (fallthrough)
  //   → join node → expect SymReg
  printSliceResult("RAX", x86::RAX, 0x400C, g);
  std::cout << '\n';
}

// -----------------------------------------------------------------------
// Example 5 — slicing a flag register (ZF after CMP)
//
//   0x5000:  cmp rax, rbx    48 39 D8
//   0x5003:  ret             C3
//
// The lifter computes ZF = EQ(rax, rbx).  Slicing ZF reconstructs that
// comparison expression.
// -----------------------------------------------------------------------

static void exampleFlagSlice() {
  printSeparator('=');
  std::cout << "  Example 5 — flag slice (ZF after CMP)\n";
  printSeparator('=');

  const std::vector<std::uint8_t> bytes = {
          0x48,
          0x39,
          0xD8, // cmp rax, rbx  @ 0x5000
          0xC3, // ret           @ 0x5003
  };
  auto g = disassemble(bytes, 0x5000);

  printAllBlocks(g);
  std::cout << '\n';
  printSliceResult("ZF", x86::ZF, 0x5000, g);
  std::cout << '\n';
}

// -----------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------

int main() {
  exampleConstant();
  exampleIndirectJump();
  exampleCrossBlock();
  exampleJoinNode();
  exampleFlagSlice();
  return 0;
}
