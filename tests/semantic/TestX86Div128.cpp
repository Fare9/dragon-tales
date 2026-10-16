#include "X86SemanticRoundTripFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace dragon::test;

// 64-bit DIV / IDIV divide the 128-bit RDX:RAX, so the lifter needs a 128-bit
// dividend. These run each form under Unicorn and through the lifter + JIT
// and compare the quotient (RAX) and remainder (RDX). Every input keeps the
// quotient in 64 bits, since an overflowing one raises #DE on hardware.

namespace {

struct DivInput {
  std::uint64_t rax, rdx, rbx;
};

void roundTrip(const char *name, std::vector<std::uint8_t> insn,
               const std::vector<DivInput> &inputs) {
  // push rbp ; mov rbp, rsp ; <insn> ; pop rbp ; ret
  std::vector<std::uint8_t> code = {0x55, 0x48, 0x89, 0xe5};
  code.insert(code.end(), insn.begin(), insn.end());
  code.push_back(0x5d);
  code.push_back(0xc3);

  for (const DivInput &in: inputs) {
    auto regs = defaultInitialRegs();
    setReg64(regs, x86off::RAX, in.rax);
    setReg64(regs, x86off::RDX, in.rdx);
    setReg64(regs, x86off::RBX, in.rbx);
    std::vector<std::uint8_t> mem;

    const auto expected = runUnicorn(code, regs, mem);
    const auto actual = runLifted(code, regs, mem);

    INFO(name << "  rdx:rax=0x" << std::hex << in.rdx << ":" << in.rax
              << " rbx=0x" << in.rbx);
    REQUIRE(getReg64(actual.regs, x86off::RAX) == getReg64(expected.regs, x86off::RAX));
    REQUIRE(getReg64(actual.regs, x86off::RDX) == getReg64(expected.regs, x86off::RDX));
    REQUIRE(getReg64(actual.regs, x86off::RBX) == getReg64(expected.regs, x86off::RBX));
  }
}

} // namespace

TEST_CASE("Semantic round-trip - 64-bit DIV divides the 128-bit RDX:RAX",
          "[semantic][x86_64][vmp]") {
  roundTrip("div rbx", {0x48, 0xf7, 0xf3},
            {
                    {100, 0, 7},
                    {0, 0, 5},
                    {0xffffffffffffffffull, 0, 1},
                    // RDX non-zero: the part a 64-bit dividend would lose.
                    {0x123456789abcdef0ull, 0x1234, 0x100000000ull},
                    {0, 1, 2},
                    // Largest quotient: (2^64 - 1) with RDX = divisor - 1.
                    {0xffffffffffffffffull, 0xfffffffffffffffeull,
                     0xffffffffffffffffull},
                    {0x8000000000000000ull, 0x7fffffffffffffffull,
                     0x8000000000000001ull},
            });
}

TEST_CASE("Semantic round-trip - 64-bit IDIV divides the signed 128-bit RDX:RAX",
          "[semantic][x86_64][vmp]") {
  constexpr std::uint64_t kMinus1 = 0xffffffffffffffffull;
  roundTrip("idiv rbx", {0x48, 0xf7, 0xfb},
            {
                    {100, 0, 7},
                    {static_cast<std::uint64_t>(-100), kMinus1, 7},
                    {100, 0, static_cast<std::uint64_t>(-7)},
                    {static_cast<std::uint64_t>(-100), kMinus1,
                     static_cast<std::uint64_t>(-7)},
                    // A negative dividend that needs all 128 bits.
                    {0x0000000000000001ull, 0xfffffffffffffff0ull, 0x100000000000ull},
            });
}
