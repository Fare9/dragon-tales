#include <catch2/catch_test_macros.hpp>
#include <dragon/Disassembler.hpp>

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static dragon::Disassembler makeX86_64() {
  dragon::Configuration config;
  config.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  return dragon::Disassembler(dragon::Architecture::X86_64, config);
}

// -----------------------------------------------------------------------
// Valid / Invalid architecture
// -----------------------------------------------------------------------

TEST_CASE("Disassembler is valid for supported architectures",
          "[disassembler]") {
  dragon::Configuration config;

  SECTION("x86_64") {
    dragon::Disassembler dis(dragon::Architecture::X86_64, config);
    REQUIRE(dis.isValid());
  }

  SECTION("x86_32") {
    dragon::Disassembler dis(dragon::Architecture::X86_32, config);
    REQUIRE(dis.isValid());
  }

  SECTION("ARM64") {
    dragon::Disassembler dis(dragon::Architecture::ARM64, config);
    REQUIRE(dis.isValid());
  }

  SECTION("Unknown architecture") {
    dragon::Disassembler dis(dragon::Architecture::Unknown, config);
    REQUIRE_FALSE(dis.isValid());
  }
}

// -----------------------------------------------------------------------
// Empty / edge cases
// -----------------------------------------------------------------------

TEST_CASE("Disassembler edge cases", "[disassembler]") {
  auto dis = makeX86_64();

  SECTION("empty bytes returns empty list") {
    const std::vector<std::uint8_t> bytes = {};
    auto instrs = dis.disassemble(bytes, 0x1000);
    REQUIRE(instrs.empty());
  }

  SECTION("single nop instruction") {
    // 0x90 = nop
    const std::vector<std::uint8_t> bytes = {0x90};
    auto instrs = dis.disassemble(bytes, 0x1000);
    REQUIRE(instrs.size() == 1);
    REQUIRE(instrs[0].address() == 0x1000);
    REQUIRE(instrs[0].size() == 1);
  }
}

// -----------------------------------------------------------------------
// Instruction fields
// -----------------------------------------------------------------------

TEST_CASE("Disassembler linear sweep x86_64", "[disassembler]") {
  auto dis = makeX86_64();

  // nop; ret
  const std::vector<std::uint8_t> bytes = {0x90, 0xC3};
  auto instrs = dis.disassemble(bytes, 0x1000);

  REQUIRE(instrs.size() == 2);
  REQUIRE(instrs[0].address() == 0x1000);
  REQUIRE(instrs[0].mnemonic() == "nop");
  REQUIRE(instrs[1].address() == 0x1001);
  REQUIRE(instrs[1].isReturn());
}

TEST_CASE("Instruction fields are correct", "[disassembler][instruction]") {
  auto dis = makeX86_64();

  SECTION("mov rax, rcx — address, size, mnemonic, operands") {
    // 48 89 C8 = mov rax, rcx  (Intel syntax)
    const std::vector<std::uint8_t> bytes = {0x48, 0x89, 0xC8};
    auto instrs = dis.disassemble(bytes, 0x1000);

    REQUIRE(instrs.size() == 1);
    const auto &instr = instrs[0];

    REQUIRE(instr.address() == 0x1000);
    REQUIRE(instr.size() == 3);
    REQUIRE(instr.mnemonic() == "mov");
    REQUIRE_FALSE(instr.isJump());
    REQUIRE_FALSE(instr.isCall());
    REQUIRE_FALSE(instr.isReturn());
    REQUIRE(instr.branches().empty());
  }

  SECTION("raw bytes are preserved") {
    const std::vector<std::uint8_t> bytes = {0x48, 0x89, 0xC8};
    auto instrs = dis.disassemble(bytes, 0x1000);

    REQUIRE(instrs.size() == 1);
    REQUIRE(instrs[0].bytes() == bytes);
  }

  SECTION("address increments correctly across instructions") {
    // nop (1 byte), nop (1 byte), ret (1 byte)
    const std::vector<std::uint8_t> bytes = {0x90, 0x90, 0xC3};
    auto instrs = dis.disassemble(bytes, 0x2000);

    REQUIRE(instrs.size() == 3);
    REQUIRE(instrs[0].address() == 0x2000);
    REQUIRE(instrs[1].address() == 0x2001);
    REQUIRE(instrs[2].address() == 0x2002);
  }
}

// -----------------------------------------------------------------------
// Branch detection
// -----------------------------------------------------------------------

TEST_CASE("Branch detection", "[disassembler][instruction][branch]") {
  auto dis = makeX86_64();

  SECTION("unconditional direct jump — isBranch, has target") {
    // EB 05 = jmp +5 (relative), target = 0x1002 + 5 = 0x1007
    const std::vector<std::uint8_t> bytes = {0xEB, 0x05};
    auto instrs = dis.disassemble(bytes, 0x1000);

    REQUIRE(instrs.size() == 1);
    const auto &instr = instrs[0];

    REQUIRE(instr.isJump());
    REQUIRE_FALSE(instr.isCall());
    REQUIRE_FALSE(instr.isReturn());
    REQUIRE_FALSE(instr.branches().empty());
    REQUIRE(instr.branches().contains(0x1007));
  }

  SECTION("conditional branch je — isBranch, has target") {
    // 74 05 = je +5, target = 0x1002 + 5 = 0x1007
    const std::vector<std::uint8_t> bytes = {0x74, 0x05};
    auto instrs = dis.disassemble(bytes, 0x1000);

    REQUIRE(instrs.size() == 1);
    const auto &instr = instrs[0];

    REQUIRE(instr.isJump());
    REQUIRE_FALSE(instr.isCall());
    REQUIRE_FALSE(instr.isReturn());
    REQUIRE_FALSE(instr.branches().empty());
  }

  SECTION("indirect jump — isBranch, no target") {
    // FF E0 = jmp rax  (indirect)
    const std::vector<std::uint8_t> bytes = {0xFF, 0xE0};
    auto instrs = dis.disassemble(bytes, 0x1000);

    REQUIRE(instrs.size() == 1);
    const auto &instr = instrs[0];

    REQUIRE(instr.isJump());
    REQUIRE(instr.branches().empty());
  }
}

// -----------------------------------------------------------------------
// Call detection
// -----------------------------------------------------------------------

TEST_CASE("Call detection", "[disassembler][instruction][call]") {
  auto dis = makeX86_64();

  SECTION("direct call — isCall, has target") {
    // E8 00 00 00 00 = call +0 (relative), target = 0x1005
    const std::vector<std::uint8_t> bytes = {0xE8, 0x00, 0x00, 0x00, 0x00};
    auto instrs = dis.disassemble(bytes, 0x1000);

    REQUIRE(instrs.size() == 1);
    const auto &instr = instrs[0];

    REQUIRE(instr.isCall());
    REQUIRE_FALSE(instr.isJump());
    REQUIRE_FALSE(instr.isReturn());
  }

  SECTION("indirect call — isCall, no branch target") {
    // FF D0 = call rax  (indirect)
    const std::vector<std::uint8_t> bytes = {0xFF, 0xD0};
    auto instrs = dis.disassemble(bytes, 0x1000);

    REQUIRE(instrs.size() == 1);
    const auto &instr = instrs[0];

    REQUIRE(instr.isCall());
    REQUIRE(instr.branches().empty());
  }
}

// -----------------------------------------------------------------------
// Return detection
// -----------------------------------------------------------------------

TEST_CASE("Return detection", "[disassembler][instruction][return]") {
  auto dis = makeX86_64();

  SECTION("ret — isReturn") {
    // C3 = ret
    const std::vector<std::uint8_t> bytes = {0xC3};
    auto instrs = dis.disassemble(bytes, 0x1000);

    REQUIRE(instrs.size() == 1);
    const auto &instr = instrs[0];

    REQUIRE(instr.isReturn());
    REQUIRE_FALSE(instr.isJump());
    REQUIRE_FALSE(instr.isCall());
    REQUIRE(instr.branches().empty());
  }

  SECTION("ret imm16 — isReturn") {
    // C2 08 00 = ret 8
    const std::vector<std::uint8_t> bytes = {0xC2, 0x08, 0x00};
    auto instrs = dis.disassemble(bytes, 0x1000);

    REQUIRE(instrs.size() == 1);
    REQUIRE(instrs[0].isReturn());
  }
}
