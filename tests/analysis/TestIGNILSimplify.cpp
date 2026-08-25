#include <catch2/catch_test_macros.hpp>

#include <dragon/lifters/ignil/IGNIL.hpp>
#include <dragon/lifters/ignil/IGNILSimplify.hpp>
#include <dragon/lifters/ignil/x86/x86IGNILRegs.hpp>

namespace x86 = dragon::x86;
using dragon::ignil::Block;
using dragon::ignil::Const;
using dragon::ignil::Op;
using dragon::ignil::Opcode;
using dragon::ignil::simplifyBlock;
using dragon::ignil::SimplifyStats;
using dragon::ignil::Temp;
using dragon::ignil::Value;

namespace {
constexpr std::uint64_t kAddr = 0x1000;
}

// ---------------------------------------------------------------------------
// Constant folding
// ---------------------------------------------------------------------------

TEST_CASE("IGNILSimplify - folds ADD of two Consts", "[ignil][simplify]") {
  Block block{kAddr};
  block.append({Opcode::ADD, Value{x86::RAX},
                {Value{Const{5, 64}}, Value{Const{3, 64}}}, kAddr});
  block.append({Opcode::RET, std::nullopt, {}, kAddr + 3});

  SimplifyStats stats;
  Block out = simplifyBlock(block, stats);

  REQUIRE(stats.constantsFolded >= 1);
  REQUIRE(out.size() == 2);
  const Op &op0 = out.ops()[0];
  REQUIRE(op0.opcode == Opcode::COPY);
  REQUIRE(std::holds_alternative<Const>(op0.srcs[0]));
  REQUIRE(std::get<Const>(op0.srcs[0]).value == 8);
}

TEST_CASE("IGNILSimplify - signed/unsigned comparisons fold to 1-bit result",
          "[ignil][simplify]") {
  Block block{kAddr};
  // t0 = SLT(-1:i32, 1:i32)  ->  true, since -1 < 1 signed
  Temp t0{0, 1};
  block.append({Opcode::SLT, Value{t0},
                {Value{Const{0xFFFFFFFFull, 32}}, Value{Const{1, 32}}}, kAddr});
  block.append({Opcode::COPY, Value{x86::RAX}, {Value{t0}}, kAddr + 4});
  block.append({Opcode::RET, std::nullopt, {}, kAddr + 8});

  Block out = simplifyBlock(block);
  REQUIRE(out.size() == 2); // SLT+COPY collapse into one COPY, RET stays
  const Op &op0 = out.ops()[0];
  REQUIRE(op0.opcode == Opcode::COPY);
  REQUIRE(std::get<Const>(op0.srcs[0]).value == 1);
  REQUIRE(std::get<Const>(op0.srcs[0]).width == 1);
}

TEST_CASE("IGNILSimplify - unsigned comparison of the same bits differs from signed",
          "[ignil][simplify]") {
  Block block{kAddr};
  // ULT(0xFFFFFFFF:i32, 1:i32) -> false, unsigned 0xFFFFFFFF is huge
  Temp t0{0, 1};
  block.append({Opcode::ULT, Value{t0},
                {Value{Const{0xFFFFFFFFull, 32}}, Value{Const{1, 32}}}, kAddr});
  block.append({Opcode::COPY, Value{x86::RAX}, {Value{t0}}, kAddr + 4});
  block.append({Opcode::RET, std::nullopt, {}, kAddr + 8});

  Block out = simplifyBlock(block);
  const Op &op0 = out.ops()[0];
  REQUIRE(std::get<Const>(op0.srcs[0]).value == 0);
}

// ---------------------------------------------------------------------------
// Identity / annihilator rewrites
// ---------------------------------------------------------------------------

TEST_CASE("IGNILSimplify - x+0 collapses to COPY x", "[ignil][simplify]") {
  Block block{kAddr};
  Temp t0{0, 64};
  block.append({Opcode::ADD, Value{t0},
                {Value{x86::RBX}, Value{Const{0, 64}}}, kAddr});
  block.append({Opcode::COPY, Value{x86::RAX}, {Value{t0}}, kAddr + 3});
  block.append({Opcode::RET, std::nullopt, {}, kAddr + 6});

  Block out = simplifyBlock(block);
  REQUIRE(out.size() == 2);
  const Op &op0 = out.ops()[0];
  REQUIRE(op0.opcode == Opcode::COPY);
  REQUIRE(std::holds_alternative<dragon::ignil::Reg>(op0.srcs[0]));
  REQUIRE(std::get<dragon::ignil::Reg>(op0.srcs[0]).offset == x86::RBX.offset);
}

TEST_CASE("IGNILSimplify - x*1, x&-1, x|0, x^0, x<<0 all collapse to COPY x",
          "[ignil][simplify]") {
  struct Case {
    Opcode op;
    std::uint64_t constVal;
  };
  const Case cases[] = {
          {Opcode::MUL, 1},
          {Opcode::AND, 0xFFFFFFFFFFFFFFFFull},
          {Opcode::OR, 0},
          {Opcode::XOR, 0},
          {Opcode::SHL, 0},
  };
  for (const auto &c: cases) {
    Block block{kAddr};
    block.append({c.op, Value{x86::RAX},
                  {Value{x86::RBX}, Value{Const{c.constVal, 64}}}, kAddr});
    block.append({Opcode::RET, std::nullopt, {}, kAddr + 3});

    Block out = simplifyBlock(block);
    REQUIRE(out.size() == 2);
    const Op &op0 = out.ops()[0];
    REQUIRE(op0.opcode == Opcode::COPY);
    REQUIRE(std::holds_alternative<dragon::ignil::Reg>(op0.srcs[0]));
  }
}

TEST_CASE("IGNILSimplify - x*0 and x&0 collapse to Const 0",
          "[ignil][simplify]") {
  for (Opcode op: {Opcode::MUL, Opcode::AND}) {
    Block block{kAddr};
    block.append({op, Value{x86::RAX},
                  {Value{x86::RBX}, Value{Const{0, 64}}}, kAddr});
    block.append({Opcode::RET, std::nullopt, {}, kAddr + 3});

    Block out = simplifyBlock(block);
    const Op &op0 = out.ops()[0];
    REQUIRE(op0.opcode == Opcode::COPY);
    REQUIRE(std::holds_alternative<Const>(op0.srcs[0]));
    REQUIRE(std::get<Const>(op0.srcs[0]).value == 0);
  }
}

TEST_CASE("IGNILSimplify - x|-1 collapses to Const all-ones",
          "[ignil][simplify]") {
  Block block{kAddr};
  block.append({Opcode::OR, Value{x86::RAX},
                {Value{x86::RBX}, Value{Const{0xFFFFFFFFFFFFFFFFull, 64}}},
                kAddr});
  block.append({Opcode::RET, std::nullopt, {}, kAddr + 3});

  Block out = simplifyBlock(block);
  const Op &op0 = out.ops()[0];
  REQUIRE(op0.opcode == Opcode::COPY);
  REQUIRE(std::get<Const>(op0.srcs[0]).value == 0xFFFFFFFFFFFFFFFFull);
}

// ---------------------------------------------------------------------------
// Half-adder identity: ADD(XOR(p,q), 2*AND(p,q)) -> ADD(p,q). LLVM's
// InstCombine does not fold this on its own (verified against real LLVM via
// opt-21 -O3 on a minimal repro) - see the header comment in IGNILSimplify
// for the closely related identities LLVM does fold in isolation.
// ---------------------------------------------------------------------------

TEST_CASE("IGNILSimplify - half-adder via SHL(and,1) collapses to ADD",
          "[ignil][simplify]") {
  Block block{kAddr};
  Temp tXor{0, 32};
  Temp tAnd{1, 32};
  Temp tShl{2, 32};
  Temp tSum{3, 32};
  block.append({Opcode::XOR, Value{tXor},
                {Value{x86::EBX}, Value{x86::ECX}}, kAddr});
  block.append({Opcode::AND, Value{tAnd},
                {Value{x86::EBX}, Value{x86::ECX}}, kAddr + 2});
  block.append({Opcode::SHL, Value{tShl},
                {Value{tAnd}, Value{Const{1, 8}}}, kAddr + 4});
  block.append({Opcode::ADD, Value{tSum}, {Value{tXor}, Value{tShl}},
                kAddr + 6});
  block.append({Opcode::COPY, Value{x86::EAX}, {Value{tSum}}, kAddr + 8});
  block.append({Opcode::RET, std::nullopt, {}, kAddr + 10});

  SimplifyStats stats;
  Block out = simplifyBlock(block, stats);

  REQUIRE(stats.identitiesApplied >= 1);
  // XOR/AND/SHL all become dead once the outer ADD is rewritten to
  // reference EBX/ECX directly instead of tXor/tShl - COPY doesn't fold
  // into ADD's result (ADD has no Const/Reg-copy shortcut), so what
  // survives is exactly: the rewritten ADD, the trailing COPY, and RET.
  REQUIRE(out.size() == 3);
  const Op &op0 = out.ops()[0];
  REQUIRE(op0.opcode == Opcode::ADD);
  REQUIRE(op0.srcs.size() == 2);
  bool sawEBX = false, sawECX = false;
  for (const auto &s: op0.srcs) {
    if (const auto *r = std::get_if<dragon::ignil::Reg>(&s)) {
      if (r->offset == x86::EBX.offset) sawEBX = true;
      if (r->offset == x86::ECX.offset) sawECX = true;
    }
  }
  REQUIRE(sawEBX);
  REQUIRE(sawECX);
  REQUIRE(out.ops()[1].opcode == Opcode::COPY);
  REQUIRE(out.ops()[2].opcode == Opcode::RET);
}

TEST_CASE("IGNILSimplify - half-adder via self-ADD doubling (add eax,eax "
          "shape) collapses to ADD",
          "[ignil][simplify]") {
  // Mirrors the real x86 lift of `add eax, eax` for the x2 step (self-add,
  // not a shl) - the actual shape found in tests/toy-vm/ToyVM.hpp's
  // op_add/op_sub handlers.
  Block block{kAddr};
  Temp tXor{0, 32};
  Temp tAnd{1, 32};
  Temp tDouble{2, 32};
  Temp tSum{3, 32};
  block.append({Opcode::XOR, Value{tXor},
                {Value{x86::EBX}, Value{x86::ECX}}, kAddr});
  // AND's operands are swapped relative to XOR's (ECX,EBX vs EBX,ECX) -
  // exercises the `swappedOrder` branch, not just `swapped` (which side of
  // the outer ADD the doubled/xor term is on).
  block.append({Opcode::AND, Value{tAnd},
                {Value{x86::ECX}, Value{x86::EBX}}, kAddr + 2});
  block.append({Opcode::ADD, Value{tDouble}, {Value{tAnd}, Value{tAnd}},
                kAddr + 4});
  // Doubled side first, xor side second - exercises the `swapped` loop
  // branch in tryHalfAdderIdentity.
  block.append({Opcode::ADD, Value{tSum}, {Value{tDouble}, Value{tXor}},
                kAddr + 6});
  block.append({Opcode::COPY, Value{x86::EAX}, {Value{tSum}}, kAddr + 8});
  block.append({Opcode::RET, std::nullopt, {}, kAddr + 10});

  SimplifyStats stats;
  Block out = simplifyBlock(block, stats);

  REQUIRE(stats.identitiesApplied >= 1);
  // XOR/AND/self-ADD all become dead once the outer ADD is rewritten to
  // reference EBX/ECX directly instead of tXor/tDouble - COPY doesn't fold
  // into ADD's result, so what survives is the rewritten ADD, the trailing
  // COPY, and RET.
  REQUIRE(out.size() == 3);
  REQUIRE(out.ops()[0].opcode == Opcode::ADD);
  REQUIRE(out.ops()[1].opcode == Opcode::COPY);
  REQUIRE(out.ops()[2].opcode == Opcode::RET);
}

TEST_CASE("IGNILSimplify - half-adder identity works through Temp operands "
          "too (mirrors op_sub's negate-then-add-identity chain)",
          "[ignil][simplify]") {
  Block block{kAddr};
  Temp tNegB{0, 32}; // stand-in for `sub edx,ebx` computing -b
  Temp tXor{1, 32};
  Temp tAnd{2, 32};
  Temp tDouble{3, 32};
  Temp tSum{4, 32};
  block.append({Opcode::NEG, Value{tNegB}, {Value{x86::ECX}}, kAddr});
  block.append({Opcode::XOR, Value{tXor},
                {Value{x86::EBX}, Value{tNegB}}, kAddr + 2});
  block.append({Opcode::AND, Value{tAnd},
                {Value{x86::EBX}, Value{tNegB}}, kAddr + 4});
  block.append({Opcode::ADD, Value{tDouble}, {Value{tAnd}, Value{tAnd}},
                kAddr + 6});
  block.append({Opcode::ADD, Value{tSum}, {Value{tXor}, Value{tDouble}},
                kAddr + 8});
  block.append({Opcode::COPY, Value{x86::EAX}, {Value{tSum}}, kAddr + 10});
  block.append({Opcode::RET, std::nullopt, {}, kAddr + 12});

  SimplifyStats stats;
  Block out = simplifyBlock(block, stats);
  REQUIRE(stats.identitiesApplied >= 1);
  // NEG(ECX) is still needed (feeds the collapsed ADD) - only the
  // XOR/AND/double chain should have collapsed away.
  bool foundXor = false, foundAnd = false;
  for (const Op &op: out.ops()) {
    if (op.opcode == Opcode::XOR) foundXor = true;
    if (op.opcode == Opcode::AND) foundAnd = true;
  }
  REQUIRE_FALSE(foundXor);
  REQUIRE_FALSE(foundAnd);
}

TEST_CASE("IGNILSimplify - near-miss half-adder shapes are NOT collapsed",
          "[ignil][simplify]") {
  SECTION("shift amount is 2, not 1") {
    Block block{kAddr};
    Temp tXor{0, 32};
    Temp tAnd{1, 32};
    Temp tShl{2, 32};
    Temp tSum{3, 32};
    block.append({Opcode::XOR, Value{tXor},
                  {Value{x86::EBX}, Value{x86::ECX}}, kAddr});
    block.append({Opcode::AND, Value{tAnd},
                  {Value{x86::EBX}, Value{x86::ECX}}, kAddr + 2});
    block.append({Opcode::SHL, Value{tShl},
                  {Value{tAnd}, Value{Const{2, 8}}}, kAddr + 4});
    block.append({Opcode::ADD, Value{tSum}, {Value{tXor}, Value{tShl}},
                  kAddr + 6});
    block.append({Opcode::COPY, Value{x86::EAX}, {Value{tSum}}, kAddr + 8});
    block.append({Opcode::RET, std::nullopt, {}, kAddr + 10});

    Block out = simplifyBlock(block);
    bool foundXor = false;
    for (const Op &op: out.ops())
      if (op.opcode == Opcode::XOR)
        foundXor = true;
    REQUIRE(foundXor); // not collapsed - shift amount doesn't match
  }

  SECTION("AND operands don't match XOR operands") {
    Block block{kAddr};
    Temp tXor{0, 32};
    Temp tAnd{1, 32};
    Temp tDouble{2, 32};
    Temp tSum{3, 32};
    block.append({Opcode::XOR, Value{tXor},
                  {Value{x86::EBX}, Value{x86::ECX}}, kAddr});
    block.append({Opcode::AND, Value{tAnd},
                  {Value{x86::EBX}, Value{x86::EDX}}, kAddr + 2}); // EDX, not ECX
    block.append({Opcode::ADD, Value{tDouble}, {Value{tAnd}, Value{tAnd}},
                  kAddr + 4});
    block.append({Opcode::ADD, Value{tSum}, {Value{tXor}, Value{tDouble}},
                  kAddr + 6});
    block.append({Opcode::COPY, Value{x86::EAX}, {Value{tSum}}, kAddr + 8});
    block.append({Opcode::RET, std::nullopt, {}, kAddr + 10});

    Block out = simplifyBlock(block);
    bool foundXor = false;
    for (const Op &op: out.ops())
      if (op.opcode == Opcode::XOR)
        foundXor = true;
    REQUIRE(foundXor); // not collapsed - operands don't match
  }
}

// ---------------------------------------------------------------------------
// Double negation (Temp only) and no-op width conversions
// ---------------------------------------------------------------------------

TEST_CASE("IGNILSimplify - NEG(NEG(temp)) collapses to COPY of the original",
          "[ignil][simplify]") {
  Block block{kAddr};
  Temp t0{0, 64};
  Temp t1{1, 64};
  block.append({Opcode::NEG, Value{t0}, {Value{x86::RBX}}, kAddr});
  block.append({Opcode::NEG, Value{t1}, {Value{t0}}, kAddr + 3});
  block.append({Opcode::COPY, Value{x86::RAX}, {Value{t1}}, kAddr + 6});
  block.append({Opcode::RET, std::nullopt, {}, kAddr + 9});

  Block out = simplifyBlock(block);
  // Both NEGs and the trailing COPY should collapse to one COPY(RBX).
  REQUIRE(out.size() == 2);
  const Op &op0 = out.ops()[0];
  REQUIRE(op0.opcode == Opcode::COPY);
  REQUIRE(std::holds_alternative<dragon::ignil::Reg>(op0.srcs[0]));
  REQUIRE(std::get<dragon::ignil::Reg>(op0.srcs[0]).offset == x86::RBX.offset);
}

TEST_CASE("IGNILSimplify - NEG(NEG(reg)) is NOT collapsed (Reg is not "
          "single-assignment - scope cut)",
          "[ignil][simplify]") {
  Block block{kAddr};
  Temp t0{0, 64};
  // t0 = NEG(rbx); rax = NEG(rbx)  -- deliberately NOT NEG(t0), so this is
  // really testing that a Reg operand to NEG is never looked up in
  // unaryDefOf (only Temp operands are). Use a case that *would* need
  // Reg-based def tracking to collapse: NEG(NEG(rbx)) via two ops sharing
  // the Reg operand directly is not something this pass attempts.
  block.append({Opcode::NEG, Value{t0}, {Value{x86::RBX}}, kAddr});
  block.append({Opcode::NEG, Value{x86::RAX}, {Value{x86::RBX}}, kAddr + 3});
  block.append({Opcode::RET, std::nullopt, {}, kAddr + 6});

  Block out = simplifyBlock(block);
  // t0's NEG is dead (never read) and gets DCE'd, but rax's NEG(rbx) must
  // remain a NEG - there is no rewrite rule that fires here.
  bool foundNegRax = false;
  for (const Op &op: out.ops()) {
    if (op.opcode == Opcode::NEG && op.dst &&
        std::holds_alternative<dragon::ignil::Reg>(*op.dst)) {
      foundNegRax = true;
    }
  }
  REQUIRE(foundNegRax);
}

TEST_CASE("IGNILSimplify - same-width TRUNC/ZEXT/SEXT collapse to COPY",
          "[ignil][simplify]") {
  for (Opcode op: {Opcode::ZEXT, Opcode::SEXT, Opcode::TRUNC}) {
    Block block{kAddr};
    Temp t0{0, 64};
    block.append({op, Value{t0}, {Value{x86::RBX}}, kAddr});
    block.append({Opcode::COPY, Value{x86::RAX}, {Value{t0}}, kAddr + 3});
    block.append({Opcode::RET, std::nullopt, {}, kAddr + 6});

    Block out = simplifyBlock(block);
    REQUIRE(out.size() == 2);
    REQUIRE(out.ops()[0].opcode == Opcode::COPY);
  }
}

// ---------------------------------------------------------------------------
// Dead-Temp elimination
// ---------------------------------------------------------------------------

TEST_CASE("IGNILSimplify - unused Temp-defining op is removed",
          "[ignil][simplify]") {
  Block block{kAddr};
  Temp dead{0, 64};
  block.append({Opcode::ADD, Value{dead},
                {Value{x86::RBX}, Value{x86::RCX}}, kAddr});
  block.append({Opcode::RET, std::nullopt, {}, kAddr + 3});

  SimplifyStats stats;
  Block out = simplifyBlock(block, stats);
  REQUIRE(stats.opsRemoved >= 1);
  REQUIRE(out.size() == 1);
  REQUIRE(out.ops()[0].opcode == Opcode::RET);
}

TEST_CASE("IGNILSimplify - unused Reg-defining op is kept (block-local scope "
          "cut - Reg may be live-out)",
          "[ignil][simplify]") {
  Block block{kAddr};
  // rax is written but never read within this block.
  block.append({Opcode::ADD, Value{x86::RAX},
                {Value{x86::RBX}, Value{x86::RCX}}, kAddr});
  block.append({Opcode::RET, std::nullopt, {}, kAddr + 3});

  Block out = simplifyBlock(block);
  REQUIRE(out.size() == 2);
  REQUIRE(out.ops()[0].opcode == Opcode::ADD);
}

// ---------------------------------------------------------------------------
// Fixed-point convergence
// ---------------------------------------------------------------------------

TEST_CASE("IGNILSimplify - chained folds converge to a single COPY",
          "[ignil][simplify]") {
  Block block{kAddr};
  Temp t0{0, 64};
  Temp t1{1, 64};
  // t0 = ADD(5, 3)      -> folds to Const 8
  // t1 = MUL(t0, 1)     -> once t0 is known-const 8, this both folds (8*1)
  //                        AND matches the x*1 identity; either way -> 8
  // rax = COPY(t1)      -> propagates to Const 8
  block.append({Opcode::ADD, Value{t0},
                {Value{Const{5, 64}}, Value{Const{3, 64}}}, kAddr});
  block.append({Opcode::MUL, Value{t1},
                {Value{t0}, Value{Const{1, 64}}}, kAddr + 3});
  block.append({Opcode::COPY, Value{x86::RAX}, {Value{t1}}, kAddr + 6});
  block.append({Opcode::RET, std::nullopt, {}, kAddr + 9});

  SimplifyStats stats;
  Block out = simplifyBlock(block, stats);

  REQUIRE(out.size() == 2);
  const Op &op0 = out.ops()[0];
  REQUIRE(op0.opcode == Opcode::COPY);
  REQUIRE(std::holds_alternative<Const>(op0.srcs[0]));
  REQUIRE(std::get<Const>(op0.srcs[0]).value == 8);
}

TEST_CASE("IGNILSimplify - block with nothing to simplify is left unchanged",
          "[ignil][simplify]") {
  Block block{kAddr};
  block.append({Opcode::ADD, Value{x86::RAX},
                {Value{x86::RBX}, Value{x86::RCX}}, kAddr});
  block.append({Opcode::COPY, Value{x86::RDX}, {Value{x86::RAX}}, kAddr + 3});
  block.append({Opcode::RET, std::nullopt, {}, kAddr + 6});

  SimplifyStats stats;
  Block out = simplifyBlock(block, stats);
  REQUIRE_FALSE(stats.changed());
  REQUIRE(out.size() == block.size());
}
