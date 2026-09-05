// Dead-store elimination on Jonathan Salwan's VMProtect junk-code example.
//
// Reproduces, at the IGNIL level, the collapse shown in Triton's block
// dead-store-elimination write-up: a wall of junk instructions (rcl/shld/
// setcc/and/... writing registers and flags that are immediately overwritten)
// interleaved with the real work - a run of pops ending in ret - reduces to
// just the pops + popfq + ret. Every junk write is overwritten before it is
// ever read (by a later junk write, a pop, or the popfq that reloads all
// flags), so it carries no information and its producing op is removed.
//
// See tests/analysis/TestDeadStoreElimination.cpp for the asserted version,
// and dragon::ignil::deadStoreEliminate (IGNILSimplify.hpp) for the pass.
//
// The original addresses are non-contiguous (VMProtect threads the block
// through jumps); we lay the shown instruction bytes out contiguously and
// linear-sweep them into one block - DSE acts on the instruction sequence,
// not the layout.

#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>
#include <dragon/lifters/ignil/IGNILSimplify.hpp>
#include <dragon/lifters/ignil/x86/x86IGNILRegs.hpp>

#include <cstdint>
#include <format>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

static void printSeparator(char c = '-', int width = 72) {
  std::cout << std::string(width, c) << '\n';
}

// Print a block as instruction disassembly, each followed by the IGNIL ops it
// produced. `mnemonicAt`/`disasmAt` map machine addresses back to the text.
static void printBlockByInstruction(
        const std::string &title, const dragon::ignil::Block &block,
        const std::unordered_map<std::uint64_t, std::string> &disasmAt) {
  printSeparator('=');
  std::cout << "  " << title << "  (" << block.size() << " IGNIL ops)\n";
  printSeparator('=');

  std::uint64_t lastAddr = ~0ull;
  for (const dragon::ignil::Op &op: block) {
    if (op.address != lastAddr) {
      auto it = disasmAt.find(op.address);
      std::cout << std::format("\n  {:016x}  {}\n", op.address,
                               it == disasmAt.end() ? "<synthetic>"
                                                    : it->second);
      lastAddr = op.address;
    }
    std::cout << "                      -> " << dragon::ignil::toString(op)
              << '\n';
  }
  std::cout << '\n';
}

int main() {
  const std::vector<std::uint8_t> bytes = {
          0x66, 0xD3, 0xD7,             // rcl   di, cl
          0x58,                         // pop   rax
          0x66, 0x41, 0x0F, 0xA4, 0xDB, 0x01, // shld  r11w, bx, 1
          0x41, 0x5B,                   // pop   r11
          0x80, 0xE6, 0xCA,             // and   dh, 0CAh
          0x66, 0xF7, 0xD7,             // not   di
          0x5F,                         // pop   rdi
          0x66, 0x41, 0xC1, 0xC1, 0x0C, // rol   r9w, 0Ch
          0xF9,                         // stc
          0x41, 0x58,                   // pop   r8
          0xF5,                         // cmc
          0xF8,                         // clc
          0x66, 0x41, 0xC1, 0xE1, 0x0B, // shl   r9w, 0Bh
          0x5A,                         // pop   rdx
          0x66, 0x81, 0xF9, 0xEB, 0xD2, // cmp   cx, 0D2EBh
          0x48, 0x0F, 0xA3, 0xF1,       // bt    rcx, rsi
          0x41, 0x59,                   // pop   r9
          0x66, 0x41, 0x21, 0xE2,       // and   r10w, sp
          0x41, 0xC1, 0xD2, 0x10,       // rcl   r10d, 10h
          0x41, 0x5A,                   // pop   r10
          0x66, 0x0F, 0xBA, 0xF9, 0x0C, // btc   cx, 0Ch
          0x49, 0x0F, 0xCC,             // bswap r12
          0x48, 0x3D, 0x97, 0x74, 0x7D, 0xC7, // cmp rax, 0FFFFFFFFC77D7497h
          0x41, 0x5C,                   // pop   r12
          0x66, 0xD3, 0xC1,             // rol   cx, cl
          0xF5,                         // cmc
          0x66, 0x0F, 0xBA, 0xF5, 0x01, // btr   bp, 1
          0x66, 0x41, 0xD3, 0xFE,       // sar   r14w, cl
          0x5D,                         // pop   rbp
          0x66, 0x41, 0x29, 0xF6,       // sub   r14w, si
          0x66, 0x09, 0xF6,             // or    si, si
          0x01, 0xC6,                   // add   esi, eax
          0x66, 0x0F, 0xC1, 0xCE,       // xadd  si, cx
          0x9D,                         // popfq
          0x0F, 0x9F, 0xC1,             // setnle cl
          0x0F, 0x9E, 0xC1,             // setle cl
          0x4C, 0x0F, 0xBE, 0xF0,       // movsx r14, al
          0x59,                         // pop   rcx
          0xF7, 0xD1,                   // not   ecx
          0x59,                         // pop   rcx
          0x4C, 0x8D, 0xA8, 0xED, 0x19, 0x28, 0xC9, // lea r13, [rax-36D7E613h]
          0x66, 0xF7, 0xD6,             // not   si
          0x41, 0x5E,                   // pop   r14
          0x66, 0xF7, 0xD6,             // not   si
          0x66, 0x44, 0x0F, 0xBE, 0xEA, // movsx r13w, dl
          0x41, 0xBD, 0xB2, 0x6B, 0x48, 0xB7, // mov r13d, 0B7486BB2h
          0x5E,                         // pop   rsi
          0x66, 0x41, 0xBD, 0xCA, 0x44, // mov   r13w, 44CAh
          0x4C, 0x8D, 0xAB, 0x31, 0x11, 0x63, 0x14, // lea r13, [rbx+14631131h]
          0x41, 0x0F, 0xCD,             // bswap r13d
          0x41, 0x5D,                   // pop   r13
          0xC3,                         // ret
  };

  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;
  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  dragon::Graph g(dragon::Architecture::X86_64, cfg);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *lifted = g.getIGNILBlock(0x1000);
  if (!lifted) {
    std::cerr << "lifting failed\n";
    return 1;
  }

  // Map machine address -> disassembly text for the printouts.
  std::unordered_map<std::uint64_t, std::string> disasmAt;
  for (const dragon::BasicBlock &bb: g.blocks())
    for (const dragon::Instruction *insn: bb.instructions())
      disasmAt.emplace(insn->address(), std::string(insn->disassembly()));

  printBlockByInstruction("ORIGINAL (lifted junk block)", *lifted, disasmAt);

  // Run the pass with the conservative "assume everything is live on exit"
  // model (std::nullopt) - exactly Triton's. A store is removed only when it
  // is overwritten within the block before any read.
  // The x86-64 register file resolves a 32-bit write's zero-extend range
  // exactly (EAX -> RAX) rather than leaning on the assumed 8-byte parent.
  dragon::ignil::DeadStoreOptions opts;
  opts.registerFile = dragon::x86::registerFile();

  dragon::ignil::SimplifyStats stats;
  dragon::ignil::Block optimized =
          dragon::ignil::deadStoreEliminate(*lifted, std::nullopt, stats, opts);

  printBlockByInstruction("OPTIMIZED (after dead-store elimination)", optimized,
                          disasmAt);

  // Distil the surviving *instructions* (unique addresses, in order) - this is
  // the clean "what's left" view Triton's write-up shows.
  printSeparator('=');
  std::cout << "  SURVIVING INSTRUCTIONS\n";
  printSeparator('=');
  std::uint64_t lastAddr = ~0ull;
  for (const dragon::ignil::Op &op: optimized) {
    if (op.address == lastAddr)
      continue;
    lastAddr = op.address;
    auto it = disasmAt.find(op.address);
    if (it != disasmAt.end())
      std::cout << std::format("  {:016x}  {}\n", op.address, it->second);
  }

  std::cout << std::format(
          "\nIGNIL ops: {} -> {}  (removed {})\n", lifted->size(),
          optimized.size(), stats.opsRemoved);
  return 0;
}
