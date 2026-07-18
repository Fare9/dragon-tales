#include <catch2/catch_test_macros.hpp>

#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>
#include <dragon/lifters/ignil/arm/armIGNILRegs.hpp>

#include <vector>

namespace arm = dragon::arm;

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

static bool blockDefines(const dragon::ignil::Block &block,
                         const dragon::ignil::Reg &reg) {
  for (const dragon::ignil::Op &op: block) {
    if (op.dst && std::holds_alternative<dragon::ignil::Reg>(*op.dst)) {
      const auto &dst = std::get<dragon::ignil::Reg>(*op.dst);
      if (dst.offset == reg.offset && dst.width == reg.width)
        return true;
    }
  }
  return false;
}

// -----------------------------------------------------------------------
// CMP (SUBS with discarded destination) must set NZCV flags.
//
// Regression test: liftInstruction()'s dispatch only checked the plain
// ADD/SUB/AND opcodes, never their S-suffixed counterparts (ADDS/SUBS/ANDS -
// the encodings behind cmp/cmn/tst), so flag-setting instructions were
// silently dropped from the IGNIL block (zero ops emitted for them).
// -----------------------------------------------------------------------

TEST_CASE("ARM IGNIL lifting - cmp sets NZCV flags", "[ignil][arm64]") {
  // 0x1000: cmp w0, #5   71 00 14 1f -> LE: 1f 14 00 71
  // 0x1004: ret          c0 03 5f d6
  const std::vector<std::uint8_t> bytes = {
          0x1f, 0x14, 0x00, 0x71, // cmp w0, #5
          0xc0, 0x03, 0x5f, 0xd6, // ret
  };

  auto dis = makeDisassembler(dragon::Architecture::ARM64);
  auto g = makeGraph(dragon::Architecture::ARM64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *entry = g.getIGNILBlock(0x1000);
  REQUIRE(entry != nullptr);

  // Before the fix this block held only the RET - no flag ops at all.
  REQUIRE(entry->ops().size() > 1);

  REQUIRE(blockDefines(*entry, arm::NF));
  REQUIRE(blockDefines(*entry, arm::ZF));
  REQUIRE(blockDefines(*entry, arm::CF));
  REQUIRE(blockDefines(*entry, arm::VF));
}

// -----------------------------------------------------------------------
// Full if/else function: the entry block's cmp/b.le must lower to a CJMP
// whose condition traces back to the ZF/NF flags set by the cmp.
// -----------------------------------------------------------------------

TEST_CASE("ARM IGNIL lifting - conditional branch (cmp/b.le)",
          "[ignil][arm64]") {
  // Equivalent C:  int foo(int x) { if (x > 0) return 1; else return -1; }
  //
  // 0x1000: stp x29, x30, [sp, #-16]!   fd 7b bf a9
  // 0x1004: mov x29, sp                 fd 03 00 91
  // 0x1008: cmp w0, #0                  1f 00 00 71
  // 0x100c: b.le 0x101c                 8d 00 00 54
  // -- then-block --
  // 0x1010: mov w0, #1                  20 00 80 52
  // 0x1014: ldp x29, x30, [sp], #16     fd 7b c1 a8
  // 0x1018: ret                         c0 03 5f d6
  // -- else-block --
  // 0x101c: mov w0, #-1                 00 00 80 12
  // 0x1020: ldp x29, x30, [sp], #16     fd 7b c1 a8
  // 0x1024: ret                         c0 03 5f d6
  const std::vector<std::uint8_t> bytes = {
          0xfd, 0x7b, 0xbf, 0xa9, // stp x29, x30, [sp, #-16]!  (0x1000)
          0xfd, 0x03, 0x00, 0x91, // mov x29, sp                (0x1004)
          0x1f, 0x00, 0x00, 0x71, // cmp w0, #0                 (0x1008)
          0x8d, 0x00, 0x00, 0x54, // b.le 0x101c                (0x100c)
          0x20, 0x00, 0x80, 0x52, // mov w0, #1                 (0x1010)
          0xfd, 0x7b, 0xc1, 0xa8, // ldp x29, x30, [sp], #16    (0x1014)
          0xc0, 0x03, 0x5f, 0xd6, // ret                        (0x1018)
          0x00, 0x00, 0x80, 0x12, // mov w0, #-1                (0x101c)
          0xfd, 0x7b, 0xc1, 0xa8, // ldp x29, x30, [sp], #16    (0x1020)
          0xc0, 0x03, 0x5f, 0xd6, // ret                        (0x1024)
  };

  auto dis = makeDisassembler(dragon::Architecture::ARM64);
  auto g = makeGraph(dragon::Architecture::ARM64);
  dis.disassemble(bytes, 0x1000, g);

  const dragon::ignil::Block *entry = g.getIGNILBlock(0x1000);
  REQUIRE(entry != nullptr);
  REQUIRE_FALSE(entry->empty());

  REQUIRE(blockDefines(*entry, arm::ZF));
  REQUIRE(blockDefines(*entry, arm::NF));

  const dragon::ignil::Op &terminator = entry->ops().back();
  REQUIRE(terminator.opcode == dragon::ignil::Opcode::CJMP);
  // taken = 0x101c (else-block), fall-through = 0x1010 (then-block)
  REQUIRE(std::get<dragon::ignil::Const>(terminator.srcs[1]).value == 0x101c);
  REQUIRE(std::get<dragon::ignil::Const>(terminator.srcs[2]).value == 0x1010);
}
