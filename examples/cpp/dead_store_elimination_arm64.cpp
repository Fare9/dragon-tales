// Dead-store elimination on an AArch64 function (the x86 sibling of this
// example is dead_store_elimination.cpp, on Salwan's VMProtect block).
//
// Where the x86 example leans on the "assume everything is live on exit"
// model (Triton's), AArch64 makes the *other* mode of the pass shine: the
// AAPCS64 calling convention says only X0 (return) + the callee-saved
// registers (X19-X28, FP, SP) are observable after a `ret`. The caller-saved
// scratch registers (X9-X15) are dead, so any junk a protector scribbles into
// them - however tangled - can be dropped wholesale once you hand the pass a
// precise live-out set.
//
// The function below does real work in W0 and buries it under junk writes to
// X9-X13. We run the pass twice to show the difference:
//   * std::nullopt  (assume-all-live): removes only stores provably
//                    overwritten within the block - here just the first,
//                    immediately-clobbered `mov x9`.
//   * precise liveOut (X0 + callee-saved + SP): every scratch write is dead
//                    at the return and the whole junk chain collapses.
//
// See dragon::ignil::deadStoreEliminate (IGNILSimplify.hpp) for the pass and
// tests/analysis/TestDeadStoreElimination.cpp for the asserted version.

#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>
#include <dragon/lifters/ignil/IGNILSimplify.hpp>
#include <dragon/lifters/ignil/arm/armIGNILRegs.hpp>

#include <cstdint>
#include <format>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace arm = dragon::arm;

static void printSeparator(char c = '-', int width = 72) {
  std::cout << std::string(width, c) << '\n';
}

static void printBlock(
        const std::string &title, const dragon::ignil::Block &block,
        const std::unordered_map<std::uint64_t, std::string> &disasmAt) {
  printSeparator('=');
  std::cout << "  " << title << "  (" << block.size() << " IGNIL ops)\n";
  printSeparator('=');

  std::uint64_t lastAddr = ~0ull;
  for (const dragon::ignil::Op &op: block) {
    if (op.address != lastAddr) {
      auto it = disasmAt.find(op.address);
      std::cout << std::format("\n  {:08x}  {}\n", op.address,
                               it == disasmAt.end() ? "<synthetic>"
                                                    : it->second);
      lastAddr = op.address;
    }
    std::cout << "                -> " << dragon::ignil::toString(op) << '\n';
  }
  std::cout << '\n';
}

int main() {
  // Assembled with llvm-mc (aarch64). One straight-line block ending in ret.
  //   mov  w0, #10          real
  //   add  w0, w0, #5       real   -> W0 = 15 (the return value)
  //   mov  x9,  #0x1111     junk   (dead even under assume-all-live: the next
  //                                 instruction overwrites x9 before any read)
  //   mov  x9,  #0x2222     junk
  //   mov  x10, #0x3333     junk
  //   eor  x11, x9, x10     junk
  //   add  x12, x11, #1     junk
  //   sub  x13, x12, x9     junk
  //   ret
  const std::vector<std::uint8_t> bytes = {
          0x40, 0x01, 0x80, 0x52, // mov  w0, #10
          0x00, 0x14, 0x00, 0x11, // add  w0, w0, #5
          0x29, 0x22, 0x82, 0xd2, // mov  x9,  #0x1111
          0x49, 0x44, 0x84, 0xd2, // mov  x9,  #0x2222
          0x6a, 0x66, 0x86, 0xd2, // mov  x10, #0x3333
          0x2b, 0x01, 0x0a, 0xca, // eor  x11, x9, x10
          0x6c, 0x05, 0x00, 0x91, // add  x12, x11, #1
          0x8d, 0x01, 0x09, 0xcb, // sub  x13, x12, x9
          0xc0, 0x03, 0x5f, 0xd6, // ret
  };

  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;
  dragon::Disassembler dis(dragon::Architecture::ARM64, cfg);
  dragon::Graph g(dragon::Architecture::ARM64, cfg);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *lifted = g.getIGNILBlock(0x1000);
  if (!lifted) {
    std::cerr << "lifting failed\n";
    return 1;
  }

  std::unordered_map<std::uint64_t, std::string> disasmAt;
  for (const dragon::BasicBlock &bb: g.blocks())
    for (const dragon::Instruction *insn: bb.instructions())
      disasmAt.emplace(insn->address(), std::string(insn->disassembly()));

  printBlock("ORIGINAL (real work buried in scratch-register junk)", *lifted,
             disasmAt);

  // Hand the pass the AArch64 register file so a 32-bit write's zero-extend
  // range is resolved exactly: W0 zeroes the 8 bytes of X0, but S0 zeroes all
  // 16 bytes of V0. Without it an 8-byte parent is assumed, which is wrong for
  // the S registers (see DeadStoreOptions::registerFile).
  dragon::ignil::DeadStoreOptions opts;
  opts.registerFile = arm::registerFile();

  // (1) Conservative model: assume every register is live on exit.
  dragon::ignil::SimplifyStats allLiveStats;
  dragon::ignil::Block allLive = dragon::ignil::deadStoreEliminate(
          *lifted, std::nullopt, allLiveStats, opts);
  printBlock("AFTER DSE - assume-all-live (std::nullopt)", allLive, disasmAt);

  // (2) ABI-aware: AAPCS64 live-out at a `ret` is X0 + callee-saved + SP.
  const std::vector<dragon::ignil::Reg> liveOut = {
          arm::X0, arm::X19, arm::X20, arm::X21, arm::X22, arm::X23, arm::X24,
          arm::X25, arm::X26, arm::X27, arm::X28, arm::X29, arm::SP};
  dragon::ignil::SimplifyStats abiStats;
  dragon::ignil::Block abi =
          dragon::ignil::deadStoreEliminate(*lifted, liveOut, abiStats, opts);
  printBlock("AFTER DSE - AAPCS64 live-out (X0 + callee-saved + SP)", abi,
             disasmAt);

  printSeparator('=');
  std::cout << "  SUMMARY\n";
  printSeparator('=');
  std::cout << std::format("  assume-all-live : {} -> {} ops (removed {})\n",
                           lifted->size(), allLive.size(),
                           allLiveStats.opsRemoved);
  std::cout << std::format("  AAPCS64 live-out: {} -> {} ops (removed {})\n",
                           lifted->size(), abi.size(), abiStats.opsRemoved);
  return 0;
}
