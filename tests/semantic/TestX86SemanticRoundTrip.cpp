#include "X86SemanticRoundTripFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace dragon::test;

// Compares real (Unicorn) vs lifted (JIT) final state, ignoring the flags
// X86IGNILLifter doesn't model (see maskUnmodeledFlags in the fixture).
static void requireStatesMatch(RoundTripResult expected, RoundTripResult actual,
                               const RegBuf &initialRegs) {
  maskUnmodeledFlags(expected.regs);
  maskUnmodeledFlags(actual.regs);
  REQUIRE(expected.regs == actual.regs);
  maskSentinelSlot(expected.mem, initialRegs);
  maskSentinelSlot(actual.mem, initialRegs);
  if (expected.mem != actual.mem) {
    for (std::size_t i = 0; i < expected.mem.size(); ++i) {
      if (expected.mem[i] != actual.mem[i])
        FAIL("mem[" << i << "] (addr 0x" << std::hex << (kMemBase + i) << std::dec
                    << ") expected=" << (int) expected.mem[i]
                    << " actual=" << (int) actual.mem[i]);
    }
  }
}

// -----------------------------------------------------------------------
// Test 1: baseline sanity - mov/add register-only, no memory involved.
// Confirms the harness itself (Unicorn setup, JIT plumbing, comparison)
// works before trusting it to validate anything more interesting.
// -----------------------------------------------------------------------

TEST_CASE("Semantic round-trip - mov/add registers", "[semantic][x86_64]") {
  // push rbp ; mov rbp, rsp        - dragon-tales' function-start heuristic
  //   (X86Info::x8664bitIsCommonPrologue) requires this exact shape; without
  //   it the entry address is never recognized as a function and lift()
  //   has nothing to lift (see lib/disassemblers/x86/x86Analysis.cpp).
  // mov eax, 5 ; mov ebx, 3 ; add eax, ebx ; ret
  const std::vector<std::uint8_t> code = {
          0x55,                         // push rbp
          0x48, 0x89, 0xe5,             // mov rbp, rsp
          0xb8, 0x05, 0x00, 0x00, 0x00, // mov eax, 5
          0xbb, 0x03, 0x00, 0x00, 0x00, // mov ebx, 3
          0x01, 0xd8,                   // add eax, ebx
          0x5d,                         // pop rbp
          0xc3,                         // ret
  };
  auto initialRegs = defaultInitialRegs();
  std::vector<std::uint8_t> initialMem;

  auto expected = runUnicorn(code, initialRegs, initialMem);
  auto actual = runLifted(code, initialRegs, initialMem);

  REQUIRE(getReg64(actual.regs, x86off::RAX) == 8);
  requireStatesMatch(expected, actual, initialRegs);
}

// -----------------------------------------------------------------------
// Test 2/3: the exact RMW forms fixed for ToDo/LifterGaps.md #1 -
// `add [mem], reg` and `sub [mem], imm8` - executed for real, not just
// checked for IGNIL op shape.
// -----------------------------------------------------------------------

TEST_CASE("Semantic round-trip - add [mem], reg (RMW)", "[semantic][x86_64]") {
  // push rbp ; mov rbp, rsp ; mov dword ptr [rbp-8], 10 ;
  // add dword ptr [rbp-8], eax ; ret
  const std::vector<std::uint8_t> code = {
          0x55,                                     // push rbp
          0x48, 0x89, 0xe5,                         // mov rbp, rsp
          0xc7, 0x45, 0xf8, 0x0a, 0x00, 0x00, 0x00, // mov [rbp-8], 10
          0x01, 0x45, 0xf8,                          // add [rbp-8], eax
          0x5d,                                       // pop rbp
          0xc3,                                       // ret
  };
  auto initialRegs = defaultInitialRegs();
  setReg64(initialRegs, x86off::RAX, 5);
  std::vector<std::uint8_t> initialMem;

  auto expected = runUnicorn(code, initialRegs, initialMem);
  auto actual = runLifted(code, initialRegs, initialMem);
  requireStatesMatch(expected, actual, initialRegs);
}

TEST_CASE("Semantic round-trip - sub [mem], imm8 (RMW)", "[semantic][x86_64]") {
  // push rbp ; mov rbp, rsp ; mov dword ptr [rbp-8], 20 ;
  // sub dword ptr [rbp-8], 7 ; ret
  const std::vector<std::uint8_t> code = {
          0x55,                                     // push rbp
          0x48, 0x89, 0xe5,                         // mov rbp, rsp
          0xc7, 0x45, 0xf8, 0x14, 0x00, 0x00, 0x00, // mov [rbp-8], 20
          0x83, 0x6d, 0xf8, 0x07,                    // sub [rbp-8], 7
          0x5d,                                       // pop rbp
          0xc3,                                       // ret
  };
  auto initialRegs = defaultInitialRegs();
  std::vector<std::uint8_t> initialMem;

  auto expected = runUnicorn(code, initialRegs, initialMem);
  auto actual = runLifted(code, initialRegs, initialMem);
  requireStatesMatch(expected, actual, initialRegs);
}

// -----------------------------------------------------------------------
// Test 4: the strongest regression test for the original bug. Before the
// fix, `cmp eax, [mem]` produced zero IGNIL ops, so CJMP would branch on
// stale flags - correct by accident at best. Running both branch
// directions for real proves the flags computed from a memory operand
// actually steer control flow correctly, not just that *some* ops appear.
// -----------------------------------------------------------------------

TEST_CASE("Semantic round-trip - cmp reg, [mem] branches correctly (not taken)",
         "[semantic][x86_64]") {
  // push rbp ; mov rbp, rsp ; mov dword ptr [rbp-20], 100 ;
  // cmp eax, [rbp-20] ; jle L2
  // mov ecx, 1 ; ret                         (fallthrough: eax > 100)
  // L2: mov ecx, 2 ; ret
  const std::vector<std::uint8_t> code = {
          0x55,                                     // push rbp
          0x48, 0x89, 0xe5,                         // mov rbp, rsp
          0xc7, 0x45, 0xec, 0x64, 0x00, 0x00, 0x00, // mov [rbp-20], 100
          0x3b, 0x45, 0xec,                          // cmp eax, [rbp-20]
          0x7e, 0x07,                                 // jle +7 -> L2
          0xb9, 0x01, 0x00, 0x00, 0x00,               // mov ecx, 1
          0x5d,                                       // pop rbp
          0xc3,                                       // ret
          0xb9, 0x02, 0x00, 0x00, 0x00,               // L2: mov ecx, 2
          0x5d,                                       // pop rbp
          0xc3,                                       // ret
  };
  auto initialRegs = defaultInitialRegs();
  setReg64(initialRegs, x86off::RAX, 200); // 200 > 100: jle not taken
  std::vector<std::uint8_t> initialMem;

  auto expected = runUnicorn(code, initialRegs, initialMem);
  auto actual = runLifted(code, initialRegs, initialMem);

  REQUIRE(getReg64(actual.regs, x86off::RCX) == 1);
  requireStatesMatch(expected, actual, initialRegs);
}

TEST_CASE("Semantic round-trip - cmp reg, [mem] branches correctly (taken)",
         "[semantic][x86_64]") {
  const std::vector<std::uint8_t> code = {
          0x55,                                     // push rbp
          0x48, 0x89, 0xe5,                         // mov rbp, rsp
          0xc7, 0x45, 0xec, 0x64, 0x00, 0x00, 0x00, // mov [rbp-20], 100
          0x3b, 0x45, 0xec,                          // cmp eax, [rbp-20]
          0x7e, 0x07,                                 // jle +7 -> L2
          0xb9, 0x01, 0x00, 0x00, 0x00,               // mov ecx, 1
          0x5d,                                       // pop rbp
          0xc3,                                       // ret
          0xb9, 0x02, 0x00, 0x00, 0x00,               // L2: mov ecx, 2
          0x5d,                                       // pop rbp
          0xc3,                                       // ret
  };
  auto initialRegs = defaultInitialRegs();
  setReg64(initialRegs, x86off::RAX, 50); // 50 <= 100: jle taken
  std::vector<std::uint8_t> initialMem;

  auto expected = runUnicorn(code, initialRegs, initialMem);
  auto actual = runLifted(code, initialRegs, initialMem);

  REQUIRE(getReg64(actual.regs, x86off::RCX) == 2);
  requireStatesMatch(expected, actual, initialRegs);
}

// -----------------------------------------------------------------------
// Test 5: add reg, [mem] - the second gap scripts/audit_x86_lifter_coverage.py
// found (the reg,mem direction, distinct from the RMW mem,reg form above).
// -----------------------------------------------------------------------

TEST_CASE("Semantic round-trip - add reg, [mem]", "[semantic][x86_64]") {
  // push rbp ; mov rbp, rsp ; mov dword ptr [rbp-8], 42 ;
  // add eax, [rbp-8] ; ret
  const std::vector<std::uint8_t> code = {
          0x55,                                     // push rbp
          0x48, 0x89, 0xe5,                         // mov rbp, rsp
          0xc7, 0x45, 0xf8, 0x2a, 0x00, 0x00, 0x00, // mov [rbp-8], 42
          0x03, 0x45, 0xf8,                          // add eax, [rbp-8]
          0x5d,                                       // pop rbp
          0xc3,                                       // ret
  };
  auto initialRegs = defaultInitialRegs();
  setReg64(initialRegs, x86off::RAX, 8);
  std::vector<std::uint8_t> initialMem;

  auto expected = runUnicorn(code, initialRegs, initialMem);
  auto actual = runLifted(code, initialRegs, initialMem);

  REQUIRE(getReg64(actual.regs, x86off::RAX) == 50);
  requireStatesMatch(expected, actual, initialRegs);
}

// -----------------------------------------------------------------------
// Test: INC/DEC must not disturb CF - the whole reason they exist
// separately from ADD/SUB by 1. This is the one genuinely novel flag
// semantic in that lifter-gap batch (every other op either computes a
// flag or marks it UNDEF; this is the first "leave it completely alone"
// case), so it gets a real execution check rather than just an IGNIL-shape
// one - `requireStatesMatch` compares CF unmasked.
// -----------------------------------------------------------------------

TEST_CASE("Semantic round-trip - inc/dec leave CF exactly as a prior cmp "
          "left it",
          "[semantic][x86_64]") {
  // push rbp ; mov rbp, rsp ; mov eax, 1 ; mov ebx, 2 ; cmp eax, ebx
  //   (unsigned 1 < 2 -> CF=1) ; mov ecx, 5 ; inc ecx ; mov edx, 5 ;
  //   dec edx ; pop rbp ; ret
  const std::vector<std::uint8_t> code = {
          0x55,                         // push rbp
          0x48, 0x89, 0xe5,             // mov rbp, rsp
          0xb8, 0x01, 0x00, 0x00, 0x00, // mov eax, 1
          0xbb, 0x02, 0x00, 0x00, 0x00, // mov ebx, 2
          0x39, 0xd8,                   // cmp eax, ebx
          0xb9, 0x05, 0x00, 0x00, 0x00, // mov ecx, 5
          0xff, 0xc1,                   // inc ecx
          0xba, 0x05, 0x00, 0x00, 0x00, // mov edx, 5
          0xff, 0xca,                   // dec edx
          0x5d,                         // pop rbp
          0xc3,                         // ret
  };
  auto initialRegs = defaultInitialRegs();
  std::vector<std::uint8_t> initialMem;

  auto expected = runUnicorn(code, initialRegs, initialMem);
  auto actual = runLifted(code, initialRegs, initialMem);

  REQUIRE(expected.regs[x86off::CF] == 1); // sanity: the cmp really set it
  REQUIRE(actual.regs[x86off::CF] == 1); // ...and inc/dec kept it
  REQUIRE(getReg64(actual.regs, x86off::RCX) == 6);
  REQUIRE(getReg64(actual.regs, x86off::RDX) == 4);
  requireStatesMatch(expected, actual, initialRegs);
}

// -----------------------------------------------------------------------
// POPFQ / PUSHFQ - previously lifted to zero ops (silently dropped, which
// also corrupted the RSP chain). Validated for real against Unicorn.
// -----------------------------------------------------------------------

TEST_CASE("Semantic round-trip - popfq loads flags from the stack",
          "[semantic][x86_64]") {
  // push rbp ; mov rbp, rsp ; mov eax, 0xCD5 ; push rax ; popfq ; pop rbp ; ret
  //   0xCD5 sets, at their RFLAGS bit positions, CF(0) PF(2) AF(4) ZF(6)
  //   SF(7) DF(10) OF(11) - so popfq must scatter those bits into the flag
  //   registers. push rax/popfq keep the stack balanced.
  const std::vector<std::uint8_t> code = {
          0x55,                         // push rbp
          0x48, 0x89, 0xe5,             // mov rbp, rsp
          0xb8, 0xd5, 0x0c, 0x00, 0x00, // mov eax, 0xCD5
          0x50,                         // push rax
          0x9d,                         // popfq
          0x5d,                         // pop rbp
          0xc3,                         // ret
  };
  auto initialRegs = defaultInitialRegs();
  std::vector<std::uint8_t> initialMem;

  auto expected = runUnicorn(code, initialRegs, initialMem);
  auto actual = runLifted(code, initialRegs, initialMem);

  // The modeled, non-masked flags all came from the popped 0xCD5.
  REQUIRE(actual.regs[x86off::CF] == 1);
  REQUIRE(actual.regs[x86off::ZF] == 1);
  REQUIRE(actual.regs[x86off::SF] == 1);
  REQUIRE(actual.regs[x86off::OF] == 1);
  requireStatesMatch(expected, actual, initialRegs);
}

TEST_CASE("Semantic round-trip - pushfq then popfq preserves the flags",
          "[semantic][x86_64]") {
  // push rbp ; mov rbp, rsp ; mov eax, 3 ; mov ebx, 7 ; cmp eax, ebx ;
  // pushfq ; popfq ; pop rbp ; ret
  //   cmp 3,7 borrows: CF=1, SF=1, ZF=0, OF=0. pushfq must gather those into
  //   an RFLAGS word on the stack that popfq reads back unchanged.
  const std::vector<std::uint8_t> code = {
          0x55,                         // push rbp
          0x48, 0x89, 0xe5,             // mov rbp, rsp
          0xb8, 0x03, 0x00, 0x00, 0x00, // mov eax, 3
          0xbb, 0x07, 0x00, 0x00, 0x00, // mov ebx, 7
          0x39, 0xd8,                   // cmp eax, ebx
          0x9c,                         // pushfq
          0x9d,                         // popfq
          0x5d,                         // pop rbp
          0xc3,                         // ret
  };
  auto initialRegs = defaultInitialRegs();
  std::vector<std::uint8_t> initialMem;

  auto expected = runUnicorn(code, initialRegs, initialMem);
  auto actual = runLifted(code, initialRegs, initialMem);

  REQUIRE(actual.regs[x86off::CF] == 1);
  REQUIRE(actual.regs[x86off::SF] == 1);
  REQUIRE(actual.regs[x86off::ZF] == 0);
  REQUIRE(actual.regs[x86off::OF] == 0);

  // Registers only: the pushfq'd word left on the stack legitimately differs
  // between the two sides (Unicorn stores the real RFLAGS incl. unmodeled
  // system bits like IF; the lifter stores only the modeled subset + the
  // reserved bit 1), so a full memory compare would flag a known, documented
  // modeling gap rather than a bug. The flags round-tripping correctly is the
  // property under test.
  maskUnmodeledFlags(expected.regs);
  maskUnmodeledFlags(actual.regs);
  REQUIRE(expected.regs == actual.regs);
}
