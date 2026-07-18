#include <dragon/analysis/BackwardSlicer.hpp>
#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>
#include <dragon/analysis/SliceExpr.hpp>
#include <dragon/lifters/ignil/arm/armIGNILRegs.hpp>

#include <cstdint>
#include <format>
#include <iostream>
#include <string>
#include <vector>

namespace arm = dragon::arm;

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------
//
// Same walkthrough as backward_slicing.cpp, ported to AArch64. The backward
// slicer itself is architecture-independent - only the register namespace
// and instruction encodings change.

static void printSeparator(char c = '-', int width = 72) {
  std::cout << std::string(width, c) << '\n';
}

static dragon::Graph disassemble(const std::vector<std::uint8_t> &bytes,
                                 std::uint64_t base) {
  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;

  dragon::Disassembler dis(dragon::Architecture::ARM64, cfg);
  dragon::Graph g(dragon::Architecture::ARM64, cfg);
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
//   0x1000:  mov x0, #42     40 05 80 d2
//   0x1004:  ret             c0 03 5f d6
//
// Slicing X0 at the end of the single block should resolve to Const{42}.
// -----------------------------------------------------------------------

static void exampleConstant() {
  printSeparator('=');
  std::cout << "  Example 1 — constant folding\n";
  printSeparator('=');

  const std::vector<std::uint8_t> bytes = {
          0x40, 0x05, 0x80, 0xd2, // mov x0, #42
          0xc0, 0x03, 0x5f, 0xd6, // ret
  };
  auto g = disassemble(bytes, 0x1000);

  printAllBlocks(g);
  std::cout << '\n';
  printSliceResult("X0", arm::X0, 0x1000, g);
  std::cout << '\n';
}

// -----------------------------------------------------------------------
// Example 2 — indirect jump target resolution
//
//   0x2000:  mov x0, #0x3000   00 00 86 d2
//   0x2004:  add x0, x0, #8    00 20 00 91
//   0x2008:  br x0             00 00 1f d6
//
// Slicing X0 before the indirect BR shows the full symbolic expression for
// the jump target - the same expression armAnalysis::resolveIndirectJumps
// hands to the switch detector / Z3 solver internally.
// -----------------------------------------------------------------------

static void exampleIndirectJump() {
  printSeparator('=');
  std::cout << "  Example 2 — indirect jump target\n";
  printSeparator('=');

  const std::vector<std::uint8_t> bytes = {
          0x00, 0x00, 0x86, 0xd2, // mov x0, #0x3000
          0x00, 0x20, 0x00, 0x91, // add x0, x0, #8
          0x00, 0x00, 0x1f, 0xd6, // br x0
  };
  auto g = disassemble(bytes, 0x2000);

  printAllBlocks(g);
  std::cout << '\n';
  printSliceResult("X0", arm::X0, 0x2000, g);
  std::cout << '\n';
}

// -----------------------------------------------------------------------
// Example 3 — cross-block propagation
//
//   Block A  0x3000:
//     mov x0, #100    80 0c 80 d2
//     b 0x3008        01 00 00 14
//
//   Block B  0x3008:
//     ret             c0 03 5f d6
//
// Block B has a single predecessor (A), so the slicer crosses into A and
// resolves X0 to Const{100}.
// -----------------------------------------------------------------------

static void exampleCrossBlock() {
  printSeparator('=');
  std::cout << "  Example 3 — cross-block propagation\n";
  printSeparator('=');

  const std::vector<std::uint8_t> bytes = {
          0x80, 0x0c, 0x80, 0xd2, // mov x0, #100  @ 0x3000
          0x01, 0x00, 0x00, 0x14, // b 0x3008      @ 0x3004
          0xc0, 0x03, 0x5f, 0xd6, // ret           @ 0x3008
  };
  auto g = disassemble(bytes, 0x3000);

  printAllBlocks(g);
  std::cout << '\n';
  // Slice X0 from within block B (0x3008) — should cross into A.
  printSliceResult("X0", arm::X0, 0x3008, g);
  std::cout << '\n';
}

// -----------------------------------------------------------------------
// Example 4 — join node stops propagation
//
//   Block A  0x4000:  cmp x0, x1  /  b.le 0x4010
//   Block B  0x4008:  mov x0, #1  /  b 0x4010
//   Block C  0x4010:  ret
//
// C has two predecessors (A via the taken b.le, B via the unconditional b).
// The slicer stops at the join and returns SymReg instead of guessing which
// predecessor's value reaches C.
// -----------------------------------------------------------------------

static void exampleJoinNode() {
  printSeparator('=');
  std::cout << "  Example 4 — join node stops at SymReg\n";
  printSeparator('=');

  const std::vector<std::uint8_t> bytes = {
          0x1f, 0x00, 0x01, 0xeb, // cmp x0, x1    @ 0x4000
          0x6d, 0x00, 0x00, 0x54, // b.le 0x4010   @ 0x4004
          0x20, 0x00, 0x80, 0xd2, // mov x0, #1    @ 0x4008
          0x01, 0x00, 0x00, 0x14, // b 0x4010      @ 0x400c
          0xc0, 0x03, 0x5f, 0xd6, // ret           @ 0x4010
  };
  auto g = disassemble(bytes, 0x4000);

  printAllBlocks(g);
  std::cout << '\n';

  std::cout << "  Block addresses in graph:\n";
  for (const dragon::BasicBlock &b: g.blocks())
    std::cout << std::format("    0x{:x}\n", b.startAddress());
  std::cout << '\n';

  // Slice X0 from the ret block (0x4010):
  //   0x4010 has predecessors 0x4000 (b.le taken) and 0x4008 (b)
  //   → join node → expect SymReg
  printSliceResult("X0", arm::X0, 0x4010, g);
  std::cout << '\n';
}

// -----------------------------------------------------------------------
// Example 5 — slicing a flag register (ZF after CMP)
//
//   0x5000:  cmp w0, #5    1f 14 00 71
//   0x5004:  ret           c0 03 5f d6
//
// The lifter computes ZF = EQ(SUB(w0, 5), 0). Slicing ZF reconstructs that
// comparison expression - this is what feeds condition resolution for
// b.eq/b.ne and friends.
// -----------------------------------------------------------------------

static void exampleFlagSlice() {
  printSeparator('=');
  std::cout << "  Example 5 — flag slice (ZF after CMP)\n";
  printSeparator('=');

  const std::vector<std::uint8_t> bytes = {
          0x1f, 0x14, 0x00, 0x71, // cmp w0, #5  @ 0x5000
          0xc0, 0x03, 0x5f, 0xd6, // ret         @ 0x5004
  };
  auto g = disassemble(bytes, 0x5000);

  printAllBlocks(g);
  std::cout << '\n';
  printSliceResult("ZF", arm::ZF, 0x5000, g);
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
