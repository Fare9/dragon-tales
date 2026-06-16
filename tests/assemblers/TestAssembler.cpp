#include <catch2/catch_test_macros.hpp>
#include <dragon/assembler/Assembler.hpp>
#include <dragon/disassembler/Disassembler.hpp>

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static dragon::Assembler makeAssembler(dragon::Architecture arch,
                                       dragon::Syntax syntax = dragon::Syntax::INTEL) {
  dragon::Configuration config;
  config.syntax = syntax;
  return dragon::Assembler(arch, config);
}

static dragon::Disassembler makeDisassembler(dragon::Architecture arch) {
  dragon::Configuration config;
  return dragon::Disassembler(arch, config);
}

static dragon::Assembler makeX86_64Assembler() {
  return makeAssembler(dragon::Architecture::X86_64);
}

static dragon::Disassembler makeX86_64Disassembler() {
  return makeDisassembler(dragon::Architecture::X86_64);
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

// -----------------------------------------------------------------------
// x86-32 assembly
// -----------------------------------------------------------------------

TEST_CASE("Assembler x86_32 — basic instructions", "[assembler][x86_32]") {
  auto as = makeAssembler(dragon::Architecture::X86_32);

  SECTION("nop produces one byte 0x90") {
    auto bytes = as.assemble("nop");
    REQUIRE(bytes.size() == 1);
    REQUIRE(bytes[0] == 0x90);
  }

  SECTION("ret produces one byte 0xC3") {
    auto bytes = as.assemble("ret");
    REQUIRE(bytes.size() == 1);
    REQUIRE(bytes[0] == 0xC3);
  }

  SECTION("nop and ret have distinct encodings") {
    REQUIRE(as.assemble("nop") != as.assemble("ret"));
  }
}

TEST_CASE("Assembler x86_32 — round-trip", "[assembler][disassembler][x86_32]") {
  auto as  = makeAssembler(dragon::Architecture::X86_32);
  auto dis = makeDisassembler(dragon::Architecture::X86_32);

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
// ARM64 assembly
// -----------------------------------------------------------------------

TEST_CASE("Assembler ARM64 — basic instructions", "[assembler][arm64]") {
  auto as = makeAssembler(dragon::Architecture::ARM64);

  SECTION("nop produces 4 bytes") {
    auto bytes = as.assemble("nop");
    REQUIRE(bytes.size() == 4);
  }

  SECTION("ret produces 4 bytes") {
    auto bytes = as.assemble("ret");
    REQUIRE(bytes.size() == 4);
  }

  SECTION("nop and ret have distinct encodings") {
    REQUIRE(as.assemble("nop") != as.assemble("ret"));
  }

  SECTION("multiple instructions produce 8 bytes") {
    auto bytes = as.assemble("nop\n ret");
    REQUIRE(bytes.size() == 8);
  }
}

TEST_CASE("Assembler ARM64 — round-trip", "[assembler][disassembler][arm64]") {
  auto as  = makeAssembler(dragon::Architecture::ARM64);
  auto dis = makeDisassembler(dragon::Architecture::ARM64);

  SECTION("nop round-trip") {
    auto bytes = as.assemble("nop");
    REQUIRE_FALSE(bytes.empty());
    auto instrs = dis.disassemble(bytes, 0x1000);
    REQUIRE(instrs.size() == 1);
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
    REQUIRE(instrs[1].isReturn());
  }
}

// -----------------------------------------------------------------------
// AT&T syntax (x86-64)
// -----------------------------------------------------------------------

TEST_CASE("Assembler x86_64 AT&T syntax", "[assembler][x86_64][syntax]") {
  auto intelAsm = makeAssembler(dragon::Architecture::X86_64, dragon::Syntax::INTEL);
  auto attAsm   = makeAssembler(dragon::Architecture::X86_64, dragon::Syntax::ATT);

  SECTION("nop produces same bytes regardless of syntax") {
    // nop has no operands — encoding is syntax-independent
    REQUIRE(intelAsm.assemble("nop") == attAsm.assemble("nop"));
  }

  SECTION("ret produces same bytes regardless of syntax") {
    REQUIRE(intelAsm.assemble("ret") == attAsm.assemble("ret"));
  }
}
