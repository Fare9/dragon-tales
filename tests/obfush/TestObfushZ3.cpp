#ifdef DRAGON_WITH_Z3

#include <catch2/catch_test_macros.hpp>

#include <dragon/analysis/BackwardSlicer.hpp>
#include <dragon/analysis/SliceExprZ3.hpp>
#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/x86/x86IGNILRegs.hpp>

#include <cstdint>
#include <vector>

namespace x86 = dragon::x86;
using namespace dragon::slice;

namespace {

// Same fixture as tests/obfush/TestObfush.cpp - see that file for the exact
// compiled instructions this comes from.
const std::vector<std::uint8_t> kComputeBytes = {
        0xf3, 0x0f, 0x1e, 0xfa,
        0xba, 0x1f, 0x00, 0x00, 0x00,
        0x29, 0xf2,
        0xbe, 0x17, 0x00, 0x00, 0x00,
        0x29, 0xfe,
        0xbf, 0x9b, 0x60, 0xdd, 0xff,
        0xe9, 0x64, 0xff, 0xff, 0xff,
};

constexpr std::uint64_t kBaseAddr = 0x1000;
constexpr std::uint32_t kSaltNum1 = 23;

} // namespace

// ---------------------------------------------------------------------------
// Prove - not just structurally match, but algebraically prove via Z3 - that
// the masking formula BackwardSlicer recovered for num1 really is the exact
// inverse of the real unmask step obfus.h's shared dispatch function does
// (`num1 -= SALT_NUM1; num1 *= -1;`, i.e. `-(masked) + SALT_NUM1`).
//
// This is stronger than the structural match in TestObfush.cpp: it checks
// the recovered expression is correct for every possible value of the
// caller's parameter `a`, not just that it has the right shape.
// ---------------------------------------------------------------------------

TEST_CASE("Obfush - Z3 proves the recovered num1 masking formula really "
          "inverts to the caller's own parameter",
          "[obfush][z3]") {
  dragon::Configuration cfg;
  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  std::vector<dragon::Instruction> insns =
          dis.disassemble(kComputeBytes, kBaseAddr);
  dragon::ignil::Block block = dis.liftTrace(insns);

  dragon::Graph g{dragon::Architecture::X86_64, dragon::Configuration{}};
  g.addIGNILBlock(std::move(block));
  g.addBlock(kBaseAddr);

  dragon::BackwardSlicer slicer;
  Expr recoveredMasked =
          slicer.slice(dragon::ignil::Value{x86::ESI}, kBaseAddr, g);

  z3::context ctx;
  SliceExprZ3 lowerer{ctx};

  z3::expr masked = lowerer.lower(recoveredMasked);
  // Same SymReg{EDI} the recovered expression references internally -
  // SliceExprZ3 caches symbols by register key, so this resolves to the
  // exact same Z3 variable, not a fresh unrelated one.
  z3::expr a = lowerer.lower(Expr{SymReg{x86::EDI}});

  z3::expr unmasked = -masked + ctx.bv_val(kSaltNum1, 32u);

  z3::solver s{ctx};
  s.add(unmasked != a);
  REQUIRE(s.check() == z3::unsat);
}

#endif // DRAGON_WITH_Z3
