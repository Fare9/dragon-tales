#include <catch2/catch_test_macros.hpp>

#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>

#include <algorithm>
#include <vector>

// -----------------------------------------------------------------------
// Helper
// -----------------------------------------------------------------------

static dragon::Disassembler makeDisassembler(dragon::Architecture arch) {
  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;
  return dragon::Disassembler(arch, cfg);
}

static bool hasOpcode(const dragon::ignil::Block &block,
                      dragon::ignil::Opcode opcode) {
  const auto &ops = block.ops();
  return std::find_if(ops.begin(), ops.end(), [&](const auto &op) {
           return op.opcode == opcode;
         }) != ops.end();
}

// -----------------------------------------------------------------------
// Basic: liftTrace on a straight-line sequence produces the same ops as
// liftBlock would, without ever touching a Graph/BasicBlock.
// -----------------------------------------------------------------------

TEST_CASE("liftTrace - lifts a plain linear sequence", "[trace][x86_64]") {
  // mov eax, 5 ; mov ebx, 3 ; add eax, ebx ; ret
  const std::vector<std::uint8_t> bytes = {
          0xb8, 0x05, 0x00, 0x00, 0x00, // mov eax, 5
          0xbb, 0x03, 0x00, 0x00, 0x00, // mov ebx, 3
          0x01, 0xd8,                   // add eax, ebx
          0xc3,                         // ret
  };
  auto dis = makeDisassembler(dragon::Architecture::X86_64);

  // The CFG-free decode overload - exactly the intended trace source.
  auto instructions = dis.disassemble(bytes, 0x1000);
  REQUIRE_FALSE(instructions.empty());

  auto block = dis.liftTrace(instructions);
  REQUIRE_FALSE(block.empty());
  REQUIRE(block.startAddress() == 0x1000);
  REQUIRE(hasOpcode(block, dragon::ignil::Opcode::ADD));
  REQUIRE(hasOpcode(block, dragon::ignil::Opcode::RET));
}

TEST_CASE("liftTrace - reuses the exact same instruction lifting as "
         "liftBlock (memory RMW form)",
         "[trace][x86_64]") {
  // add dword ptr [rbp-8], eax ; ret - the RMW form fixed for
  // ToDo/LifterGaps.md #1. liftTrace must produce the same LOAD/ADD/STORE
  // shape as the Graph-based path, since it dispatches through the same
  // per-instruction lifter.
  const std::vector<std::uint8_t> bytes = {
          0x01, 0x45, 0xf8, // add [rbp-8], eax
          0xc3,             // ret
  };
  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto instructions = dis.disassemble(bytes, 0x1000);

  auto block = dis.liftTrace(instructions);
  REQUIRE(hasOpcode(block, dragon::ignil::Opcode::LOAD));
  REQUIRE(hasOpcode(block, dragon::ignil::Opcode::ADD));
  REQUIRE(hasOpcode(block, dragon::ignil::Opcode::STORE));
}

// -----------------------------------------------------------------------
// The actual point of liftTrace: a sequence that does NOT form a valid,
// analyzable block/CFG - instructions decoded at unrelated addresses,
// stitched together the way a real captured execution trace would be
// (DBI/emulator single-stepping, jumping wherever the program actually
// went). X86Analysis's block/function discovery would never accept this
// as one coherent region; liftTrace doesn't ask.
// -----------------------------------------------------------------------

TEST_CASE("liftTrace - lifts a non-contiguous, CFG-incoherent sequence",
         "[trace][x86_64]") {
  auto dis = makeDisassembler(dragon::Architecture::X86_64);

  // Two unrelated one-instruction "snapshots" from very different
  // addresses - nothing connects them structurally.
  const std::vector<std::uint8_t> firstBytes = {0x48, 0x01, 0xd8}; // add rax, rbx
  const std::vector<std::uint8_t> secondBytes = {0x48, 0x29, 0xd1}; // sub rcx, rdx
  auto first = dis.disassemble(firstBytes, 0x1000);
  auto second = dis.disassemble(secondBytes, 0x9000);
  REQUIRE(first.size() == 1);
  REQUIRE(second.size() == 1);

  std::vector<dragon::Instruction> trace;
  trace.push_back(std::move(first.front()));
  trace.push_back(std::move(second.front()));

  auto block = dis.liftTrace(trace);
  // Both instructions were lifted independently and in trace order,
  // despite 0x9000 not being any kind of successor of 0x1000.
  REQUIRE(block.startAddress() == 0x1000);
  REQUIRE(hasOpcode(block, dragon::ignil::Opcode::ADD));
  REQUIRE(hasOpcode(block, dragon::ignil::Opcode::SUB));
}

TEST_CASE("liftTrace - no fall-through terminator is synthesized",
         "[trace][x86_64]") {
  // Unlike liftBlock, a trace ending on a non-branch instruction must NOT
  // get an invented JMP - trace semantics say "this is exactly what
  // happened", and there is no Graph/successor to invent one from anyway.
  const std::vector<std::uint8_t> bytes = {
          0x48, 0x01, 0xd8, // add rax, rbx  (not a terminator)
  };
  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto instructions = dis.disassemble(bytes, 0x1000);

  auto block = dis.liftTrace(instructions);
  REQUIRE_FALSE(block.empty());
  REQUIRE_FALSE(hasOpcode(block, dragon::ignil::Opcode::JMP));
}

TEST_CASE("liftTrace - empty trace yields an empty block", "[trace][x86_64]") {
  auto dis = makeDisassembler(dragon::Architecture::X86_64);
  auto block = dis.liftTrace({});
  REQUIRE(block.empty());
}

// -----------------------------------------------------------------------
// ARM64 - same entry point, same guarantees.
// -----------------------------------------------------------------------

TEST_CASE("liftTrace - lifts a plain linear sequence (ARM64)", "[trace][arm64]") {
  // add w0, w0, #1 ; ret
  const std::vector<std::uint8_t> bytes = {
          0x00, 0x04, 0x00, 0x11, // add w0, w0, #1
          0xc0, 0x03, 0x5f, 0xd6, // ret
  };
  auto dis = makeDisassembler(dragon::Architecture::ARM64);
  auto instructions = dis.disassemble(bytes, 0x2000);

  auto block = dis.liftTrace(instructions);
  REQUIRE_FALSE(block.empty());
  REQUIRE(block.startAddress() == 0x2000);
  REQUIRE(hasOpcode(block, dragon::ignil::Opcode::RET));
}
