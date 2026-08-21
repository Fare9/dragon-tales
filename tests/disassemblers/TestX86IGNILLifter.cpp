#include <catch2/catch_test_macros.hpp>

#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>

#include <algorithm>
#include <iostream>
#include <vector>

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static dragon::Disassembler makeDisassembler(dragon::Architecture arch) {
  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;
  return dragon::Disassembler(arch, cfg);
}

static dragon::Graph makeGraph(dragon::Architecture arch) {
  return dragon::Graph(arch, dragon::Configuration{});
}

// Print each instruction followed by the IGNIL ops it produced,
// grouped by block.
static void printIgnilComparison(dragon::Graph &g) {
  for (const dragon::BasicBlock &block: g.blocks()) {
    std::cout << "\n--- Block @ 0x" << std::hex << block.startAddress()
              << " ---\n";

    const dragon::ignil::Block *ignilBlock =
            g.getIGNILBlock(block.startAddress());

    for (const dragon::Instruction *insn: block.instructions()) {
      std::cout << "  0x" << std::hex << insn->address() << "  "
                << insn->disassembly() << "\n";

      if (ignilBlock) {
        for (const dragon::ignil::Op &op: *ignilBlock) {
          if (op.address == insn->address())
            std::cout << "      " << dragon::ignil::toString(op) << "\n";
        }
      }
    }
  }
  std::cout << std::dec << "\n";
}

// -----------------------------------------------------------------------
// Test 1: arithmetic - MOV, ADD, SUB, XOR, RET
// -----------------------------------------------------------------------

TEST_CASE("IGNIL lifting - arithmetic block", "[ignil][x86_64]") {
  // 0x1000: 55            push rbp
  // 0x1001: 48 89 E5      mov rbp, rsp
  // 0x1004: 48 01 D8      add rax, rbx
  // 0x1007: 48 29 D1      sub rcx, rdx
  // 0x100A: 31 C0         xor eax, eax
  // 0x100C: 5D            pop rbp
  // 0x100D: C3            ret
  const std::vector<std::uint8_t> bytes = {
          0x55,                          // push rbp
          0x48, 0x89, 0xE5,              // mov rbp, rsp
          0x48, 0x01, 0xD8,              // add rax, rbx
          0x48, 0x29, 0xD1,              // sub rcx, rdx
          0x31, 0xC0,                    // xor eax, eax
          0x5D,                          // pop rbp
          0xC3,                          // ret
  };

  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  REQUIRE_FALSE(g.blocks().empty());

  const dragon::ignil::Block *ignilBlock = g.getIGNILBlock(0x1000);
  REQUIRE(ignilBlock != nullptr);
  REQUIRE_FALSE(ignilBlock->empty());

  std::cout << "\n=== IGNIL Lifting: arithmetic block ===";
  printIgnilComparison(g);
}

// -----------------------------------------------------------------------
// Test 2: LEA with complex addressing - base + index*scale + disp
// -----------------------------------------------------------------------

TEST_CASE("IGNIL lifting - LEA with SIB addressing", "[ignil][x86_64]") {
  // 0x1000: 55                  push rbp
  // 0x1001: 48 89 E5            mov rbp, rsp
  // 0x1004: 48 8D 44 8B 08      lea rax, [rbx + rcx*4 + 8]
  // 0x1009: 5D                  pop rbp
  // 0x100A: C3                  ret
  const std::vector<std::uint8_t> bytes = {
          0x55,                          // push rbp
          0x48, 0x89, 0xE5,              // mov rbp, rsp
          0x48, 0x8D, 0x44, 0x8B, 0x08, // lea rax, [rbx + rcx*4 + 8]
          0x5D,                          // pop rbp
          0xC3,                          // ret
  };

  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  REQUIRE_FALSE(g.blocks().empty());

  const dragon::ignil::Block *ignilBlock = g.getIGNILBlock(0x1000);
  REQUIRE(ignilBlock != nullptr);
  REQUIRE_FALSE(ignilBlock->empty());

  std::cout << "\n=== IGNIL Lifting: LEA with SIB addressing ===";
  printIgnilComparison(g);
}

// -----------------------------------------------------------------------
// Test 3: conditional branch - CMP + JLE produces correct CJMP condition
// -----------------------------------------------------------------------

TEST_CASE("IGNIL lifting - conditional branch (cmp/jle)", "[ignil][x86_64]") {
  // 0x1000: 48 39 D8   cmp rax, rbx
  // 0x1003: 7E 05      jle 0x100A          (offset +5 from next insn 0x1005)
  // 0x1005: 48 01 D8   add rax, rbx        (then-block)
  // 0x1008: C3         ret
  // 0x1009: 90         nop                 (pad to align else-block)
  // 0x100A: C3         ret                 (else-block)
  const std::vector<std::uint8_t> bytes = {
          0x48, 0x39, 0xD8,  // cmp rax, rbx  (0x1000)
          0x7E, 0x05,        // jle 0x100A    (0x1003)
          0x48, 0x01, 0xD8,  // add rax, rbx  (0x1005)
          0xC3,              // ret           (0x1008)
          0x90,              // nop           (0x1009)
          0xC3,              // ret           (0x100A)
  };

  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  auto blocks = g.blocks();
  REQUIRE(blocks.size() == 3);

  // All three blocks must have IGNIL representations
  REQUIRE(g.getIGNILBlock(0x1000) != nullptr); // entry: cmp + jle
  REQUIRE(g.getIGNILBlock(0x1005) != nullptr); // then: add + ret
  REQUIRE(g.getIGNILBlock(0x100A) != nullptr); // else: ret

  // The entry block's last op must be a CJMP
  const dragon::ignil::Block *entry = g.getIGNILBlock(0x1000);
  REQUIRE_FALSE(entry->empty());
  const dragon::ignil::Op &terminator = entry->ops().back();
  REQUIRE(terminator.opcode == dragon::ignil::Opcode::CJMP);
  // taken = 0x100A, fall-through = 0x1005
  REQUIRE(std::get<dragon::ignil::Const>(terminator.srcs[1]).value == 0x100A);
  REQUIRE(std::get<dragon::ignil::Const>(terminator.srcs[2]).value == 0x1005);

  std::cout << "\n=== IGNIL Lifting: conditional branch (cmp/jle) ===";
  printIgnilComparison(g);
}

// -----------------------------------------------------------------------
// Test 4: indirect jump through memory - jmp [rax]
// -----------------------------------------------------------------------

TEST_CASE("IGNIL lifting - indirect jump through memory (jmp [rax])", "[ignil][x86_64]") {
  // 0x1000: FF 20   jmp [rax]
  // 0x1002: C3      ret       (unreachable; gives the block a terminator)
  const std::vector<std::uint8_t> bytes = {
          0xFF, 0x20,  // jmp [rax]
          0xC3,        // ret
  };

  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g   = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *ib = g.getIGNILBlock(0x1000);
  REQUIRE(ib != nullptr);
  REQUIRE_FALSE(ib->empty());

  // There must be a LOAD op somewhere in the block (dereference of [rax])
  const auto &ops = ib->ops();
  auto loadIt = std::find_if(ops.begin(), ops.end(),
          [](const auto &op) { return op.opcode == dragon::ignil::Opcode::LOAD; });
  REQUIRE(loadIt != ops.end());

  // The JMP op must exist and its target must be the Temp produced by LOAD
  auto jmpIt = std::find_if(ops.begin(), ops.end(),
          [](const auto &op) { return op.opcode == dragon::ignil::Opcode::JMP; });
  REQUIRE(jmpIt != ops.end());
  REQUIRE(std::holds_alternative<dragon::ignil::Temp>(jmpIt->srcs[0]));

  std::cout << "\n=== IGNIL Lifting: indirect jump through memory (jmp [rax]) ===";
  printIgnilComparison(g);
}

// -----------------------------------------------------------------------
// Test 5: indirect call through memory - call [rbx + rcx*8]
// -----------------------------------------------------------------------

TEST_CASE("IGNIL lifting - indirect call through memory (call [rbx + rcx*8])", "[ignil][x86_64]") {
  // 0x1000: FF 14 CB   call [rbx + rcx*8]   (virtual dispatch / jump table)
  // 0x1003: C3         ret
  const std::vector<std::uint8_t> bytes = {
          0xFF, 0x14, 0xCB,  // call [rbx + rcx*8]
          0xC3,              // ret
  };

  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g   = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *ib = g.getIGNILBlock(0x1000);
  REQUIRE(ib != nullptr);
  REQUIRE_FALSE(ib->empty());

  // There must be a LOAD op (dereference of the computed address)
  const auto &ops = ib->ops();
  auto loadIt = std::find_if(ops.begin(), ops.end(),
          [](const auto &op) { return op.opcode == dragon::ignil::Opcode::LOAD; });
  REQUIRE(loadIt != ops.end());

  // The CALL op must exist and its target must be the Temp produced by LOAD
  auto callIt = std::find_if(ops.begin(), ops.end(),
          [](const auto &op) { return op.opcode == dragon::ignil::Opcode::CALL; });
  REQUIRE(callIt != ops.end());
  REQUIRE(std::holds_alternative<dragon::ignil::Temp>(callIt->srcs[0]));

  std::cout << "\n=== IGNIL Lifting: indirect call through memory (call [rbx + rcx*8]) ===";
  printIgnilComparison(g);
}

// -----------------------------------------------------------------------
// Test 6: a block that falls straight into the next block (no explicit
// jmp/ret at its end) must still get an explicit JMP terminator.
//
// Regression test: liftBlock() only emitted a JMP/CJMP/RET when the last
// machine instruction in the block was itself a branch/return. A block
// like the one below (whose last real instruction is just `mov`, with
// execution falling through into the next block) produced an IGNIL block
// with no terminator at all - invalid input for LLVMLifter, which hangs
// LLVM's mem2reg pass when it computes dominator trees over a basic block
// with no terminator instruction. Same bug, same fix, as
// ARM IGNIL lifting - fall-through block gets an explicit JMP.
// -----------------------------------------------------------------------

TEST_CASE("IGNIL lifting - fall-through block gets an explicit JMP", "[ignil][x86_64]") {
  // 0x1000: cmp rax, rbx   48 39 D8
  // 0x1003: jle 0x100c     7E 07
  // 0x1005: mov rax, 1     48 C7 C0 01 00 00 00   (falls through, no jmp/ret!)
  // 0x100c: ret            C3
  const std::vector<std::uint8_t> bytes = {
          0x48, 0x39, 0xD8,                          // cmp rax, rbx  @ 0x1000
          0x7E, 0x07,                                  // jle 0x100c    @ 0x1003
          0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,  // mov rax, 1    @ 0x1005
          0xC3,                                        // ret           @ 0x100c
  };

  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *fallThroughBlock = g.getIGNILBlock(0x1005);
  REQUIRE(fallThroughBlock != nullptr);
  REQUIRE_FALSE(fallThroughBlock->empty());

  const dragon::ignil::Op &terminator = fallThroughBlock->ops().back();
  REQUIRE(terminator.opcode == dragon::ignil::Opcode::JMP);
  REQUIRE(std::get<dragon::ignil::Const>(terminator.srcs[0]).value == 0x100c);

  std::cout << "\n=== IGNIL Lifting: fall-through block gets an explicit JMP ===";
  printIgnilComparison(g);
}

// -----------------------------------------------------------------------
// Test 7: memory-destination MOV/ADD/SUB/CMP forms.
//
// Regression test for the gap documented in ToDo/LifterGaps.md #1:
// `liftInstruction()`'s dispatch only recognized register-destination
// forms of MOV/ADD/SUB/CMP. Memory-destination forms (`mov [mem], imm`,
// RMW `add [mem], reg`/`add [mem], imm`, `cmp reg, [mem]`) fell through to
// the default case and produced *zero* IGNIL ops - not even UNDEF - so
// downstream flag/value reads silently saw stale data from an earlier,
// unrelated instruction. Every case below reproduces one of the exact
// instruction forms from the gap's `sum_to_n` repro and checks that it now
// lowers to real ops instead of vanishing.
// -----------------------------------------------------------------------

// Every ADD/SUB sets CF/ZF/SF/OF explicitly (via COPY) but never computes
// PF/AF/DF, which always fall back to UNDEF - so "exactly 3 UNDEF ops" is
// the signature of a *fully* lifted arithmetic instruction. Before this
// fix, these instructions produced no ops at all (not even those 3 UNDEFs).
static std::size_t countUndef(const dragon::ignil::Block &ib) {
  return static_cast<std::size_t>(
          std::count_if(ib.ops().begin(), ib.ops().end(), [](const auto &op) {
            return op.opcode == dragon::ignil::Opcode::UNDEF;
          }));
}

static bool hasOpcode(const dragon::ignil::Block &ib,
                      dragon::ignil::Opcode opcode) {
  const auto &ops = ib.ops();
  return std::find_if(ops.begin(), ops.end(), [&](const auto &op) {
           return op.opcode == opcode;
         }) != ops.end();
}

TEST_CASE("IGNIL lifting - mov [mem], imm (MOV32mi)", "[ignil][x86_64]") {
  // mov dword ptr [rbp-4], 0 ; ret
  const std::vector<std::uint8_t> bytes = {
          0xc7, 0x45, 0xfc, 0x00, 0x00, 0x00, 0x00, // mov [rbp-4], 0
          0xc3,                                      // ret
  };
  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *ib = g.getIGNILBlock(0x1000);
  REQUIRE(ib != nullptr);
  REQUIRE_FALSE(ib->empty()); // previously: zero ops, silently
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::STORE));

  std::cout << "\n=== IGNIL Lifting: mov [mem], imm ===";
  printIgnilComparison(g);
}

TEST_CASE("IGNIL lifting - add [mem], reg RMW (ADD32mr)", "[ignil][x86_64]") {
  // add dword ptr [rbp-8], eax ; ret
  const std::vector<std::uint8_t> bytes = {
          0x01, 0x45, 0xf8, // add [rbp-8], eax
          0xc3,             // ret
  };
  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *ib = g.getIGNILBlock(0x1000);
  REQUIRE(ib != nullptr);
  REQUIRE_FALSE(ib->empty());
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::LOAD));
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::ADD));
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::STORE));
  REQUIRE(countUndef(*ib) == 3); // only PF/AF/DF - CF/ZF/SF/OF really computed

  std::cout << "\n=== IGNIL Lifting: add [mem], reg (RMW) ===";
  printIgnilComparison(g);
}

TEST_CASE("IGNIL lifting - add [mem], imm8 RMW (ADD32mi8)", "[ignil][x86_64]") {
  // add dword ptr [rbp-8], 1 ; ret
  const std::vector<std::uint8_t> bytes = {
          0x83, 0x45, 0xf8, 0x01, // add [rbp-8], 1
          0xc3,                   // ret
  };
  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *ib = g.getIGNILBlock(0x1000);
  REQUIRE(ib != nullptr);
  REQUIRE_FALSE(ib->empty());
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::LOAD));
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::ADD));
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::STORE));
  REQUIRE(countUndef(*ib) == 3);

  std::cout << "\n=== IGNIL Lifting: add [mem], imm8 (RMW) ===";
  printIgnilComparison(g);
}

TEST_CASE("IGNIL lifting - sub [mem], reg RMW (SUB32mr)", "[ignil][x86_64]") {
  // sub dword ptr [rbp-8], ecx ; ret
  const std::vector<std::uint8_t> bytes = {
          0x29, 0x4d, 0xf8, // sub [rbp-8], ecx
          0xc3,             // ret
  };
  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *ib = g.getIGNILBlock(0x1000);
  REQUIRE(ib != nullptr);
  REQUIRE_FALSE(ib->empty());
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::LOAD));
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::SUB));
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::STORE));
  REQUIRE(countUndef(*ib) == 3);

  std::cout << "\n=== IGNIL Lifting: sub [mem], reg (RMW) ===";
  printIgnilComparison(g);
}

TEST_CASE("IGNIL lifting - cmp reg, [mem] (CMP32rm)", "[ignil][x86_64]") {
  // cmp eax, dword ptr [rbp-20] ; ret
  const std::vector<std::uint8_t> bytes = {
          0x3b, 0x45, 0xec, // cmp eax, [rbp-20]
          0xc3,             // ret
  };
  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *ib = g.getIGNILBlock(0x1000);
  REQUIRE(ib != nullptr);
  REQUIRE_FALSE(ib->empty());
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::LOAD));
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::SUB));
  REQUIRE_FALSE(hasOpcode(*ib, dragon::ignil::Opcode::STORE)); // CMP never writes back
  REQUIRE(countUndef(*ib) == 3);

  std::cout << "\n=== IGNIL Lifting: cmp reg, [mem] ===";
  printIgnilComparison(g);
}

TEST_CASE("IGNIL lifting - add reg, [mem] / sub reg, [mem] (ADD32rm/SUB32rm)",
         "[ignil][x86_64]") {
  // add eax, dword ptr [rbp-8] ; sub ecx, dword ptr [rbp-8] ; ret
  // Found by scripts/audit_x86_lifter_coverage.py: the reg,mem direction of
  // the same gap class fixed in liftAddMem/liftSubMem (RMW mem,reg/mem,imm).
  const std::vector<std::uint8_t> bytes = {
          0x03, 0x45, 0xf8, // add eax, [rbp-8]
          0x2b, 0x4d, 0xf8, // sub ecx, [rbp-8]
          0xc3,             // ret
  };
  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *ib = g.getIGNILBlock(0x1000);
  REQUIRE(ib != nullptr);
  REQUIRE_FALSE(ib->empty());
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::LOAD));
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::ADD));
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::SUB));
  REQUIRE_FALSE(hasOpcode(*ib, dragon::ignil::Opcode::STORE)); // dst is a register, not memory
  REQUIRE(countUndef(*ib) == 6); // two instructions, 3 always-UNDEF flags each

  std::cout << "\n=== IGNIL Lifting: add reg, [mem] / sub reg, [mem] ===";
  printIgnilComparison(g);
}

TEST_CASE("IGNIL lifting - cmp [mem], imm8 and cmp [mem], reg", "[ignil][x86_64]") {
  // cmp dword ptr [rbp-4], 0   ; cmp dword ptr [rbp-4], ecx ; ret
  const std::vector<std::uint8_t> bytes = {
          0x83, 0x7d, 0xfc, 0x00, // cmp [rbp-4], 0
          0x39, 0x4d, 0xfc,       // cmp [rbp-4], ecx
          0xc3,                   // ret
  };
  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *ib = g.getIGNILBlock(0x1000);
  REQUIRE(ib != nullptr);
  REQUIRE_FALSE(ib->empty());
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::LOAD));
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::SUB));
  REQUIRE_FALSE(hasOpcode(*ib, dragon::ignil::Opcode::STORE));
  REQUIRE(countUndef(*ib) == 6); // two CMPs, 3 always-UNDEF flags each

  std::cout << "\n=== IGNIL Lifting: cmp [mem], imm8 / cmp [mem], reg ===";
  printIgnilComparison(g);
}

// -----------------------------------------------------------------------
// MOVSX64rr32, SAR32ri/SAR64ri, IMUL64rri32/rri8/32rri/rri8, LEA64_32r -
// found while lifting a real compiled signed-division-by-constant sequence
// (GCC's standard multiply-high lowering for `x / C`), not
// obfuscation-specific - this is ordinary output for any program dividing
// by a non-power-of-two constant. See ToDo/Roadmap.md for the full
// writeup. Byte sequences below were generated with `gcc -c` from real
// Intel-syntax assembly and verified with `objdump -d`, not hand-derived.
// -----------------------------------------------------------------------

TEST_CASE("IGNIL lifting - movsxd rax, edi (MOVSX64rr32)", "[ignil][x86_64]") {
  const std::vector<std::uint8_t> bytes = {
          0x48, 0x63, 0xc7, // movsxd rax, edi
          0xc3,             // ret
  };
  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *ib = g.getIGNILBlock(0x1000);
  REQUIRE(ib != nullptr);
  REQUIRE_FALSE(ib->empty());
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::SEXT));
  REQUIRE(countUndef(*ib) == 0); // MOVSX never affects flags

  std::cout << "\n=== IGNIL Lifting: movsxd rax, edi ===";
  printIgnilComparison(g);
}

TEST_CASE("IGNIL lifting - sar eax, 0 leaves flags untouched (SAR32ri)",
          "[ignil][x86_64]") {
  const std::vector<std::uint8_t> bytes = {
          0xc1, 0xf8, 0x00, // sar eax, 0
          0xc3,             // ret
  };
  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *ib = g.getIGNILBlock(0x1000);
  REQUIRE(ib != nullptr);
  // Real x86: shift by 0 is a pure copy, no SAR op, no flags at all - not
  // even UNDEF, since nothing was touched.
  REQUIRE_FALSE(hasOpcode(*ib, dragon::ignil::Opcode::SAR));
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::COPY));
  REQUIRE(countUndef(*ib) == 0);

  std::cout << "\n=== IGNIL Lifting: sar eax, 0 ===";
  printIgnilComparison(g);
}

TEST_CASE("IGNIL lifting - sar eax, 1 defines OF (SAR32ri)", "[ignil][x86_64]") {
  // Same C1 /7 ib encoding as `sar eax, 0` above with the immediate byte
  // changed to 1 - the assembler prefers the shorter D1 (SAR32r1) encoding
  // for a literal `sar eax, 1`, which is a different, unrelated opcode this
  // change doesn't touch; this form is still a valid, decodable SAR32ri.
  const std::vector<std::uint8_t> bytes = {
          0xc1, 0xf8, 0x01, // sar eax, 1  (SAR32ri encoding, not SAR32r1)
          0xc3,             // ret
  };
  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *ib = g.getIGNILBlock(0x1000);
  REQUIRE(ib != nullptr);
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::SAR));
  REQUIRE(countUndef(*ib) == 3); // only PF/AF/DF - CF/ZF/SF/OF all computed

  std::cout << "\n=== IGNIL Lifting: sar eax, 1 ===";
  printIgnilComparison(g);
}

TEST_CASE("IGNIL lifting - sar edi, 0x1f / sar rax, 0x26 (SAR32ri/SAR64ri, "
          "count > 1)",
          "[ignil][x86_64]") {
  const std::vector<std::uint8_t> bytes = {
          0xc1, 0xff, 0x1f,       // sar edi, 0x1f
          0x48, 0xc1, 0xf8, 0x26, // sar rax, 0x26
          0xc3,                   // ret
  };
  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *ib = g.getIGNILBlock(0x1000);
  REQUIRE(ib != nullptr);
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::SAR));
  // OF is architecturally undefined for count != 1 - two SARs, four
  // UNDEF flags (PF/AF/OF/DF) each.
  REQUIRE(countUndef(*ib) == 8);

  std::cout << "\n=== IGNIL Lifting: sar edi, 0x1f / sar rax, 0x26 ===";
  printIgnilComparison(g);
}

TEST_CASE("IGNIL lifting - 3-operand imul with immediate (IMUL64rri32/"
          "rri8/32rri/32rri8)",
          "[ignil][x86_64]") {
  const std::vector<std::uint8_t> bytes = {
          0x48, 0x69, 0xc0, 0x41, 0xdd, 0xb3, 0x20, // imul rax, rax, 0x20b3dd41
          0x48, 0x6b, 0xc3, 0x05,                   // imul rax, rbx, 5
          0x69, 0xc3, 0x40, 0x42, 0x0f, 0x00,       // imul eax, ebx, 1000000
          0x6b, 0xc3, 0x05,                         // imul eax, ebx, 5
          0xc3,                                     // ret
  };
  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *ib = g.getIGNILBlock(0x1000);
  REQUIRE(ib != nullptr);
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::MUL));
  // All flags UNDEF for every IMUL form - SF/ZF/AF/PF are architecturally
  // undefined for IMUL, CF/OF would need a double-width product IGNIL
  // doesn't model. 4 IMULs * 7 flags each.
  REQUIRE(countUndef(*ib) == 28);

  std::cout << "\n=== IGNIL Lifting: 3-operand imul with immediate ===";
  printIgnilComparison(g);
}

TEST_CASE("IGNIL lifting - lea eax, [rdx+rcx*1] truncates a 64-bit address "
          "into a 32-bit dst (LEA64_32r)",
          "[ignil][x86_64]") {
  const std::vector<std::uint8_t> bytes = {
          0x8d, 0x04, 0x0a, // lea eax, [rdx+rcx*1]
          0xc3,             // ret
  };
  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto g = makeGraph(dragon::Architecture::X86_64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *ib = g.getIGNILBlock(0x1000);
  REQUIRE(ib != nullptr);
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::TRUNC));
  REQUIRE(hasOpcode(*ib, dragon::ignil::Opcode::ADD)); // base+index address calc
  REQUIRE(countUndef(*ib) == 0); // LEA never affects flags

  std::cout << "\n=== IGNIL Lifting: lea eax, [rdx+rcx*1] ===";
  printIgnilComparison(g);
}
