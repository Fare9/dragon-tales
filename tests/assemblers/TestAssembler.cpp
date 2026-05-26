#include <catch2/catch_test_macros.hpp>
#include <dragon/Assembler.hpp>
#include <dragon/Disassembler.hpp>

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static dragon::Assembler makeX86_64Assembler() {
  dragon::Configuration config;
  return dragon::Assembler(dragon::Architecture::X86_64, config);
}

static dragon::Disassembler makeX86_64Disassembler() {
  dragon::Configuration config;
  return dragon::Disassembler(dragon::Architecture::X86_64, config);
}

// -----------------------------------------------------------------------
// Valid / Invalid
// -----------------------------------------------------------------------

TEST_CASE("Assembler is valid for supported architectures", "[assembler]") {
  dragon::Configuration config;

  SECTION("x86_64") {
    dragon::Assembler as(dragon::Architecture::X86_64, config);
    REQUIRE(as.isValid());
  }

  SECTION("x86_32") {
    dragon::Assembler as(dragon::Architecture::X86_32, config);
    REQUIRE(as.isValid());
  }

  SECTION("ARM64") {
    dragon::Assembler as(dragon::Architecture::ARM64, config);
    REQUIRE(as.isValid());
  }

  SECTION("Unknown") {
    dragon::Assembler as(dragon::Architecture::Unknown, config);
    REQUIRE_FALSE(as.isValid());
  }
}

// -----------------------------------------------------------------------
// Basic assembly
// -----------------------------------------------------------------------

TEST_CASE("Assembler produces non-empty bytes", "[assembler]") {
  auto as = makeX86_64Assembler();

  SECTION("nop") {
    auto bytes = as.assemble("nop");
    REQUIRE_FALSE(bytes.empty());
  }

  SECTION("ret") {
    auto bytes = as.assemble("ret");
    REQUIRE_FALSE(bytes.empty());
  }

  SECTION("multiple instructions") {
    auto bytes = as.assemble("nop\n ret");
    REQUIRE_FALSE(bytes.empty());
  }
}

// -----------------------------------------------------------------------
// Round-trip: assemble → disassemble
// -----------------------------------------------------------------------

TEST_CASE("Assembler round-trip x86_64", "[assembler][disassembler]") {
  auto as = makeX86_64Assembler();
  auto dis = makeX86_64Disassembler();

  SECTION("nop round-trip") {
    auto bytes = as.assemble("nop");
    REQUIRE_FALSE(bytes.empty());

    auto instrs = dis.disassemble(bytes, 0x1000);
    REQUIRE(instrs.size() == 1);
    REQUIRE(instrs[0].mnemonic() == "nop");
  }

  SECTION("ret round-trip") {
    auto bytes = as.assemble("ret");
    REQUIRE_FALSE(bytes.empty());

    auto instrs = dis.disassemble(bytes, 0x1000);
    REQUIRE(instrs.size() == 1);
    REQUIRE(instrs[0].isReturn());
  }

  SECTION("multiple instructions round-trip") {
    auto bytes = as.assemble("nop\n ret");
    REQUIRE_FALSE(bytes.empty());

    auto instrs = dis.disassemble(bytes, 0x1000);
    REQUIRE(instrs.size() == 2);
    REQUIRE(instrs[0].mnemonic() == "nop");
    REQUIRE(instrs[1].isReturn());
  }
}

// -----------------------------------------------------------------------
// Multiple calls — assemble() must be reusable
// -----------------------------------------------------------------------

TEST_CASE("Assembler can be called multiple times", "[assembler]") {
  auto as = makeX86_64Assembler();

  auto bytes1 = as.assemble("nop");
  auto bytes2 = as.assemble("ret");
  auto bytes3 = as.assemble("nop\n ret");

  REQUIRE_FALSE(bytes1.empty());
  REQUIRE_FALSE(bytes2.empty());
  REQUIRE_FALSE(bytes3.empty());
  REQUIRE(bytes1 != bytes2);
}
