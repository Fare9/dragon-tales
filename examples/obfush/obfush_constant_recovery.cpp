// Runs dragon-tales's real analysis pipeline (Disassembler -> IGNIL ->
// BackwardSlicer -> Z3) against real compiled bytes of `compute` from
// obfush_repro.c (see that file's header comment for what this reproduces
// and why - an integer-only stand-in for obfus.h's VIRT feature, since the
// real thing uses long double/x87, which X86IGNILLifter doesn't support).
//
// This is the same fixture tests/obfush/TestObfush.cpp and
// TestObfushZ3.cpp assert against - this file exists to actually show the
// analysis running and its output, narrated, rather than just pass/fail.

#include <dragon/analysis/BackwardSlicer.hpp>
#include <dragon/analysis/SliceExprZ3.hpp>
#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/x86/x86IGNILRegs.hpp>

#include <cstdint>
#include <format>
#include <iostream>
#include <string>
#include <vector>

namespace x86 = dragon::x86;
using namespace dragon::slice;

namespace {

void printHeader(const std::string &title) {
  std::cout << std::string(80, '=') << "\n  " << title << "\n"
            << std::string(80, '=') << "\n";
}

// Real compiled bytes of `compute` (gcc -O2 -c obfush_repro.c -w) - see
// tests/obfush/TestObfush.cpp for the byte-by-byte annotation.
const std::vector<std::uint8_t> kComputeBytes = {
        0xf3, 0x0f, 0x1e, 0xfa, //
        0xba, 0x1f, 0x00, 0x00, 0x00, //
        0x29, 0xf2, //
        0xbe, 0x17, 0x00, 0x00, 0x00, //
        0x29, 0xfe, //
        0xbf, 0x9b, 0x60, 0xdd, 0xff, //
        0xe9, 0x64, 0xff, 0xff, 0xff, //
};

constexpr std::uint64_t kBaseAddr = 0x1000;
constexpr std::uint32_t kSaltCmd = 0x1F4;
constexpr std::uint32_t kSaltNum1 = 23;
constexpr std::uint32_t kSaltNum2 = 31;
constexpr std::uint32_t kOpAdd = 4530;
constexpr std::uint32_t kEncryptedOpAdd =
        (kOpAdd - 1) * static_cast<std::uint32_t>(~kSaltCmd);

} // namespace

int main() {
  printHeader("obfush_repro constant recovery (real dragon-tales analysis)");
  std::cout << "Target: `compute`'s real compiled bytes from "
               "obfush_repro.c (gcc -O2).\nNo ground truth is assumed below "
               "except for printing what we already know\nfor comparison - "
               "every recovered value/expression comes from dragon-tales's\n"
               "own Disassembler + BackwardSlicer (+ Z3), not from re-"
               "deriving the answer\nby hand.\n\n";

  dragon::Configuration cfg;
  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);

  printHeader("1. Disassembly");
  std::vector<dragon::Instruction> insns =
          dis.disassemble(kComputeBytes, kBaseAddr);
  for (const auto &insn: insns)
    std::cout << std::format("  0x{:x}:  {}\n", insn.address(),
                             insn.disassembly());

  printHeader("2. Lift to IGNIL");
  dragon::ignil::Block block = dis.liftTrace(insns);
  std::cout << dragon::ignil::toString(block);

  dragon::Graph g{dragon::Architecture::X86_64, dragon::Configuration{}};
  g.addIGNILBlock(block);
  g.addBlock(kBaseAddr);

  printHeader("3. BackwardSlicer - recover each argument register");
  dragon::BackwardSlicer slicer;

  Expr command = slicer.slice(dragon::ignil::Value{x86::EDI}, kBaseAddr, g);
  Expr num1 = slicer.slice(dragon::ignil::Value{x86::ESI}, kBaseAddr, g);
  Expr num2 = slicer.slice(dragon::ignil::Value{x86::EDX}, kBaseAddr, g);

  std::cout << std::format("  EDI (command)      = {}\n", toString(command));
  std::cout << std::format("  ESI (num1, masked) = {}\n", toString(num1));
  std::cout << std::format("  EDX (num2, masked) = {}\n", toString(num2));

  printHeader("4. Check the recovered opcode against the known formula");
  if (const auto *c = std::get_if<Const>(&command.data)) {
    std::cout << std::format(
            "  Recovered: 0x{:x}\n  ENCRYPT(OP_ADD) recomputed independently: "
            "0x{:x}\n  Match: {}\n",
            c->value, kEncryptedOpAdd,
            c->value == kEncryptedOpAdd ? "YES" : "NO");
  } else {
    std::cout << "  Recovered expression was not a plain constant.\n";
  }

  printHeader("5. Z3: prove the recovered num1 formula really inverts to "
              "the caller's own parameter");
  {
    z3::context ctx;
    SliceExprZ3 lowerer{ctx};

    z3::expr masked = lowerer.lower(num1);
    z3::expr a = lowerer.lower(Expr{SymReg{x86::EDI}});
    z3::expr unmasked = -masked + ctx.bv_val(kSaltNum1, 32u);

    z3::solver s{ctx};
    s.add(unmasked != a);
    z3::check_result r = s.check();
    std::cout << std::format(
            "  Checking: for every possible value of the caller's own "
            "parameter `a`,\n  does -(recovered num1) + SALT_NUM1 == a?\n"
            "  Z3 result for the negation: {}\n"
            "  {}\n",
            r == z3::unsat ? "unsat" : "sat/unknown",
            r == z3::unsat
                    ? "-> proven: the recovered masking formula is exactly "
                      "correct for all inputs."
                    : "-> NOT proven - the recovered formula does not match "
                      "for at least one input.");
  }

  std::cout << "\nDone.\n";
  return 0;
}
