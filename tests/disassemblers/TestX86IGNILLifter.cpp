#include <catch2/catch_test_macros.hpp>

#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>

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
