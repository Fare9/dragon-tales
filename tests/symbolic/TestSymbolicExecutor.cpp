#include <catch2/catch_test_macros.hpp>

#include <dragon/lifters/ignil/IGNIL.hpp>
#include <dragon/lifters/ignil/IGNILSimplify.hpp>
#include <dragon/lifters/ignil/SymbolicExecutor.hpp>
#include <dragon/lifters/ignil/x86/x86IGNILRegs.hpp>

#include <cstring>

namespace x86 = dragon::x86;
using dragon::ignil::Block;
using dragon::ignil::Const;
using dragon::ignil::ExecStatus;
using dragon::ignil::Opcode;
using dragon::ignil::RunResult;
using dragon::ignil::simplifyBlock;
using dragon::ignil::SymbolicExecutor;
using dragon::ignil::SymbolicOptions;
using dragon::ignil::Temp;
using dragon::ignil::Value;

namespace {

constexpr std::uint64_t kAddr = 0x1000;

/// Reinterpret a double's bits, the way IGNIL's float opcodes see them.
std::uint64_t bitsOf(double d) {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &d, sizeof(bits));
  return bits;
}

double doubleOf(std::uint64_t bits) {
  double d = 0;
  std::memcpy(&d, &bits, sizeof(d));
  return d;
}

} // namespace

// ---------------------------------------------------------------------------
// Basic evaluation
// ---------------------------------------------------------------------------

TEST_CASE("SymbolicExecutor - concrete arithmetic evaluates to a constant",
          "[ignil][symbolic]") {
  // RAX = 5; RAX = RAX + 3
  Block block{kAddr};
  block.append({Opcode::COPY, Value{x86::RAX}, {Value{Const{5, 64}}}, kAddr});
  block.append({Opcode::ADD, Value{x86::RAX},
                {Value{x86::RAX}, Value{Const{3, 64}}}, kAddr + 4});
  block.append({Opcode::RET, std::nullopt, {}, kAddr + 8});

  SymbolicExecutor exec;
  RunResult r = exec.run(block);

  REQUIRE(r.ok());
  REQUIRE(r.exit == dragon::ignil::ExitKind::Return);
  REQUIRE(exec.evaluate(exec.readRegister(x86::RAX)) == 8);
}

TEST_CASE("SymbolicExecutor - a register that was never written is symbolic",
          "[ignil][symbolic]") {
  SymbolicExecutor exec;
  // No unique value: RBX can be anything.
  REQUIRE_FALSE(exec.evaluate(exec.readRegister(x86::RBX)).has_value());

  // ...unless the caller asks for a fully specified machine instead.
  SymbolicOptions opts;
  opts.symbolizeUnmappedReads = false;
  SymbolicExecutor zeroed{opts};
  REQUIRE(zeroed.evaluate(zeroed.readRegister(x86::RBX)) == 0);
}

TEST_CASE("SymbolicExecutor - seeded registers propagate through a block",
          "[ignil][symbolic]") {
  // RAX = RDI * RSI
  Block block{kAddr};
  block.append({Opcode::MUL, Value{x86::RAX},
                {Value{x86::RDI}, Value{x86::RSI}}, kAddr});

  SymbolicExecutor exec;
  exec.setRegister(x86::RDI, 7);
  exec.setRegister(x86::RSI, 6);
  REQUIRE(exec.run(block).ok());

  REQUIRE(exec.evaluate(exec.readRegister(x86::RAX)) == 42);
}

// ---------------------------------------------------------------------------
// Sub-register aliasing - the byte-granular register file
// ---------------------------------------------------------------------------

TEST_CASE("SymbolicExecutor - a 32-bit write clears the upper half",
          "[ignil][symbolic]") {
  Block block{kAddr};
  block.append({Opcode::COPY, Value{x86::EAX}, {Value{Const{0, 32}}}, kAddr});

  SymbolicExecutor exec;
  exec.setRegister(x86::RAX, 0x1122334455667788ull);
  REQUIRE(exec.run(block).ok());

  // `xor eax, eax` really does zero the whole of RAX.
  REQUIRE(exec.evaluate(exec.readRegister(x86::RAX)) == 0);
}

TEST_CASE("SymbolicExecutor - 8- and 16-bit writes merge into the parent",
          "[ignil][symbolic]") {
  SECTION("AL touches only byte 0") {
    Block block{kAddr};
    block.append({Opcode::COPY, Value{x86::AL}, {Value{Const{0xFF, 8}}}, kAddr});

    SymbolicExecutor exec;
    exec.setRegister(x86::RAX, 0x1122334455667788ull);
    REQUIRE(exec.run(block).ok());
    REQUIRE(exec.evaluate(exec.readRegister(x86::RAX)) == 0x11223344556677FFull);
  }

  SECTION("AH touches only byte 1") {
    Block block{kAddr};
    block.append({Opcode::COPY, Value{x86::AH}, {Value{Const{0xEE, 8}}}, kAddr});

    SymbolicExecutor exec;
    exec.setRegister(x86::RAX, 0x1122334455667788ull);
    REQUIRE(exec.run(block).ok());
    REQUIRE(exec.evaluate(exec.readRegister(x86::RAX)) == 0x112233445566EE88ull);
  }

  SECTION("AX touches only bytes 0-1") {
    Block block{kAddr};
    block.append({Opcode::COPY, Value{x86::AX}, {Value{Const{0xBEEF, 16}}},
                  kAddr});

    SymbolicExecutor exec;
    exec.setRegister(x86::RAX, 0x1122334455667788ull);
    REQUIRE(exec.run(block).ok());
    REQUIRE(exec.evaluate(exec.readRegister(x86::RAX)) == 0x112233445566BEEFull);
  }
}

TEST_CASE("SymbolicExecutor - sub-register reads slice the parent",
          "[ignil][symbolic]") {
  SymbolicExecutor exec;
  exec.setRegister(x86::RAX, 0x1122334455667788ull);

  REQUIRE(exec.evaluate(exec.readRegister(x86::EAX)) == 0x55667788u);
  REQUIRE(exec.evaluate(exec.readRegister(x86::AX)) == 0x7788u);
  REQUIRE(exec.evaluate(exec.readRegister(x86::AL)) == 0x88u);
  REQUIRE(exec.evaluate(exec.readRegister(x86::AH)) == 0x77u);
}

// ---------------------------------------------------------------------------
// Operation semantics
// ---------------------------------------------------------------------------

TEST_CASE("SymbolicExecutor - shifts match IGNILSimplify's constant folding",
          "[ignil][symbolic]") {
  // Both paths must agree: run the block symbolically, and fold it with
  // simplifyBlock. A disagreement means the executor and the folder model the
  // same opcode differently, which would make a simplified block behave
  // differently from the one it came from.
  struct Case {
    Opcode op;
    std::uint64_t lhs;
    std::uint64_t amount;
    dragon::ignil::Width width;
  };

  const Case cases[] = {
          {Opcode::SHL, 0xFF, 4, 8}, // in range
          {Opcode::SHL, 0xFF, 8, 8}, // amount == width
          {Opcode::SHL, 0xFF, 200, 8}, // amount past the width
          {Opcode::SHR, 0xFF, 4, 8},
          {Opcode::SHR, 0xFF, 200, 8},
          {Opcode::SAR, 0x80, 4, 8}, // negative, sign-fills
          {Opcode::SAR, 0x80, 200, 8}, // saturates to all-ones
          {Opcode::SAR, 0x7F, 200, 8}, // positive, saturates to zero
  };

  for (const Case &c : cases) {
    Block block{kAddr};
    block.append({c.op, Value{x86::AL},
                  {Value{Const{c.lhs, c.width}}, Value{Const{c.amount, 8}}},
                  kAddr});

    SymbolicExecutor exec;
    REQUIRE(exec.run(block).ok());
    const auto symbolic = exec.evaluate(exec.readRegister(x86::AL));
    REQUIRE(symbolic.has_value());

    Block folded = simplifyBlock(block);
    REQUIRE(folded.size() == 1);
    REQUIRE(folded.ops()[0].opcode == Opcode::COPY);
    REQUIRE(std::holds_alternative<Const>(folded.ops()[0].srcs[0]));
    REQUIRE(*symbolic == std::get<Const>(folded.ops()[0].srcs[0]).value);
  }
}

TEST_CASE("SymbolicExecutor - signed and unsigned division differ on the "
          "same bits",
          "[ignil][symbolic]") {
  const std::uint64_t minusEight = 0xFFFFFFFFFFFFFFF8ull;

  Block block{kAddr};
  block.append({Opcode::SDIV, Value{x86::RAX},
                {Value{Const{minusEight, 64}}, Value{Const{3, 64}}}, kAddr});
  block.append({Opcode::UDIV, Value{x86::RBX},
                {Value{Const{minusEight, 64}}, Value{Const{3, 64}}}, kAddr});
  block.append({Opcode::SMOD, Value{x86::RCX},
                {Value{Const{minusEight, 64}}, Value{Const{3, 64}}}, kAddr});

  SymbolicExecutor exec;
  REQUIRE(exec.run(block).ok());

  // -8 / 3 truncates toward zero, as C does.
  REQUIRE(exec.evaluate(exec.readRegister(x86::RAX)) == 0xFFFFFFFFFFFFFFFEull);
  REQUIRE(exec.evaluate(exec.readRegister(x86::RBX)) == (minusEight / 3));
  // -8 % 3 == -2: srem takes the sign of the dividend, unlike smod.
  REQUIRE(exec.evaluate(exec.readRegister(x86::RCX)) == 0xFFFFFFFFFFFFFFFEull);
}

TEST_CASE("SymbolicExecutor - comparisons produce a 1-bit result",
          "[ignil][symbolic]") {
  const std::uint64_t minusOne = 0xFFFFFFFFull;

  Block block{kAddr};
  Temp signedLt{0, 1};
  Temp unsignedLt{1, 1};
  // -1 < 1 signed, but 0xFFFFFFFF > 1 unsigned.
  block.append({Opcode::SLT, Value{signedLt},
                {Value{Const{minusOne, 32}}, Value{Const{1, 32}}}, kAddr});
  block.append({Opcode::ULT, Value{unsignedLt},
                {Value{Const{minusOne, 32}}, Value{Const{1, 32}}}, kAddr});

  SymbolicExecutor exec;
  REQUIRE(exec.run(block).ok());
  REQUIRE(exec.evaluate(*exec.readTemp(signedLt)) == 1);
  REQUIRE(exec.evaluate(*exec.readTemp(unsignedLt)) == 0);
}

TEST_CASE("SymbolicExecutor - SEXT and ZEXT widen differently",
          "[ignil][symbolic]") {
  Block block{kAddr};
  Temp narrow{0, 8};
  block.append({Opcode::COPY, Value{narrow}, {Value{Const{0x80, 8}}}, kAddr});
  block.append({Opcode::SEXT, Value{x86::RAX}, {Value{narrow}}, kAddr});
  block.append({Opcode::ZEXT, Value{x86::RBX}, {Value{narrow}}, kAddr});

  SymbolicExecutor exec;
  REQUIRE(exec.run(block).ok());
  REQUIRE(exec.evaluate(exec.readRegister(x86::RAX)) == 0xFFFFFFFFFFFFFF80ull);
  REQUIRE(exec.evaluate(exec.readRegister(x86::RBX)) == 0x80);
}

TEST_CASE("SymbolicExecutor - IEEE754 double arithmetic", "[ignil][symbolic]") {
  Block block{kAddr};
  block.append({Opcode::FADD, Value{x86::RAX},
                {Value{Const{bitsOf(1.5), 64}}, Value{Const{bitsOf(2.25), 64}}},
                kAddr});
  // Round-trip an integer through a double and back.
  block.append({Opcode::SITOFP, Value{x86::RBX}, {Value{Const{42, 64}}}, kAddr});
  block.append({Opcode::FPTOSI, Value{x86::RCX}, {Value{x86::RBX}}, kAddr});
  // Truncation toward zero, not rounding to nearest.
  block.append({Opcode::FPTOSI, Value{x86::RDX},
                {Value{Const{bitsOf(-3.9), 64}}}, kAddr});

  SymbolicExecutor exec;
  REQUIRE(exec.run(block).ok());

  const auto sum = exec.evaluate(exec.readRegister(x86::RAX));
  REQUIRE(sum.has_value());
  REQUIRE(doubleOf(*sum) == 3.75);

  REQUIRE(exec.evaluate(exec.readRegister(x86::RCX)) == 42);
  REQUIRE(exec.evaluate(exec.readRegister(x86::RDX)) ==
          0xFFFFFFFFFFFFFFFDull); // -3
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------

TEST_CASE("SymbolicExecutor - concrete blobs load little-endian",
          "[ignil][symbolic]") {
  SymbolicExecutor exec;
  exec.mapMemory(0x4000, {0x78, 0x56, 0x34, 0x12});

  REQUIRE(exec.evaluate(exec.readMemory(0x4000, 32)) == 0x12345678u);
  REQUIRE(exec.evaluate(exec.readMemory(0x4000, 8)) == 0x78);
  REQUIRE(exec.evaluate(exec.readMemory(0x4002, 16)) == 0x1234u);
}

TEST_CASE("SymbolicExecutor - a symbolic value survives a store/load round trip",
          "[ignil][symbolic]") {
  // [0x4000] = RDI ; RAX = [0x4000]
  Block block{kAddr};
  Temp addr{0, 64};
  block.append({Opcode::COPY, Value{addr}, {Value{Const{0x4000, 64}}}, kAddr});
  block.append({Opcode::STORE, std::nullopt, {Value{addr}, Value{x86::RDI}},
                kAddr});
  block.append({Opcode::LOAD, Value{x86::RAX}, {Value{addr}}, kAddr + 4});

  SymbolicExecutor exec;
  const z3::expr input = exec.symbolizeRegister(x86::RDI, "arg");
  REQUIRE(exec.run(block).ok());

  REQUIRE(exec.proves(exec.readRegister(x86::RAX) == input) == true);
}

TEST_CASE("SymbolicExecutor - a symbolic store address resolves to its "
          "candidates",
          "[ignil][symbolic]") {
  // [RDI] = 0xAA, where RDI is known to be either 0x100 or 0x200.
  Block block{kAddr};
  block.append({Opcode::STORE, std::nullopt,
                {Value{x86::RDI}, Value{Const{0xAA, 8}}}, kAddr});

  SymbolicExecutor exec;
  const z3::expr rdi = exec.symbolizeRegister(x86::RDI, "ptr");
  exec.assume(rdi == exec.bv(0x100, 64) || rdi == exec.bv(0x200, 64));
  exec.mapMemory(0x100, {0x00});
  exec.mapMemory(0x200, {0x00});

  RunResult r = exec.run(block);
  REQUIRE(r.complete());
  REQUIRE(r.stats.addressesConcretized == 2);

  // Neither byte is unconditionally 0xAA - each holds it only on the path
  // where RDI actually pointed there.
  REQUIRE_FALSE(exec.evaluate(exec.readMemory(0x100, 8)).has_value());
  REQUIRE(exec.proves(z3::implies(rdi == exec.bv(0x100, 64),
                                  exec.readMemory(0x100, 8) ==
                                          exec.bv(0xAA, 8))) == true);
  REQUIRE(exec.proves(z3::implies(rdi == exec.bv(0x200, 64),
                                  exec.readMemory(0x100, 8) ==
                                          exec.bv(0x00, 8))) == true);
}

TEST_CASE("SymbolicExecutor - an unconstrained store address is reported, "
          "not guessed",
          "[ignil][symbolic]") {
  Block block{kAddr};
  block.append({Opcode::STORE, std::nullopt,
                {Value{x86::RDI}, Value{Const{0xAA, 8}}}, kAddr});

  SymbolicExecutor exec;
  exec.symbolizeRegister(x86::RDI, "ptr"); // no constraint at all

  RunResult r = exec.run(block);
  REQUIRE(r.ok()); // execution continued
  REQUIRE_FALSE(r.complete()); // but the account is not complete
  REQUIRE(r.warnings.size() == 1);
  REQUIRE(r.warnings[0].address == kAddr);
}

// ---------------------------------------------------------------------------
// Proving identities - the MBA de-obfuscation case
// ---------------------------------------------------------------------------

TEST_CASE("SymbolicExecutor - proves mixed boolean-arithmetic identities",
          "[ignil][symbolic]") {
  SECTION("(a | b) + (a & b) == a + b") {
    Block block{kAddr};
    Temp orv{0, 64};
    Temp andv{1, 64};
    block.append({Opcode::OR, Value{orv}, {Value{x86::RDI}, Value{x86::RSI}},
                  kAddr});
    block.append({Opcode::AND, Value{andv}, {Value{x86::RDI}, Value{x86::RSI}},
                  kAddr});
    block.append({Opcode::ADD, Value{x86::RAX}, {Value{orv}, Value{andv}},
                  kAddr});

    SymbolicExecutor exec;
    const z3::expr a = exec.symbolizeRegister(x86::RDI, "a");
    const z3::expr b = exec.symbolizeRegister(x86::RSI, "b");
    REQUIRE(exec.run(block).ok());

    REQUIRE(exec.proves(exec.readRegister(x86::RAX) == a + b) == true);
  }

  SECTION("(a | b) - (a & b) == a ^ b") {
    Block block{kAddr};
    Temp orv{0, 64};
    Temp andv{1, 64};
    block.append({Opcode::OR, Value{orv}, {Value{x86::RDI}, Value{x86::RSI}},
                  kAddr});
    block.append({Opcode::AND, Value{andv}, {Value{x86::RDI}, Value{x86::RSI}},
                  kAddr});
    block.append({Opcode::SUB, Value{x86::RAX}, {Value{orv}, Value{andv}},
                  kAddr});

    SymbolicExecutor exec;
    const z3::expr a = exec.symbolizeRegister(x86::RDI, "a");
    const z3::expr b = exec.symbolizeRegister(x86::RSI, "b");
    REQUIRE(exec.run(block).ok());

    REQUIRE(exec.proves(exec.readRegister(x86::RAX) == (a ^ b)) == true);
  }

  SECTION("a false identity is refuted") {
    Block block{kAddr};
    block.append({Opcode::XOR, Value{x86::RAX},
                  {Value{x86::RDI}, Value{x86::RSI}}, kAddr});

    SymbolicExecutor exec;
    const z3::expr a = exec.symbolizeRegister(x86::RDI, "a");
    const z3::expr b = exec.symbolizeRegister(x86::RSI, "b");
    REQUIRE(exec.run(block).ok());

    REQUIRE(exec.proves(exec.readRegister(x86::RAX) == a + b) == false);
  }
}

TEST_CASE("SymbolicExecutor - recovers the constant an obfuscated block "
          "actually computes",
          "[ignil][symbolic]") {
  // ((x ^ 0xA5A5) + 0xA5A5) - (x ^ 0xA5A5) is x-independent: it is 0xA5A5.
  Block block{kAddr};
  Temp masked{0, 64};
  Temp shifted{1, 64};
  block.append({Opcode::XOR, Value{masked},
                {Value{x86::RDI}, Value{Const{0xA5A5, 64}}}, kAddr});
  block.append({Opcode::ADD, Value{shifted},
                {Value{masked}, Value{Const{0xA5A5, 64}}}, kAddr});
  block.append({Opcode::SUB, Value{x86::RAX}, {Value{shifted}, Value{masked}},
                kAddr});

  SymbolicExecutor exec;
  exec.symbolizeRegister(x86::RDI, "x");
  REQUIRE(exec.run(block).ok());

  REQUIRE(exec.evaluate(exec.readRegister(x86::RAX)) == 0xA5A5);
}

// ---------------------------------------------------------------------------
// Solving - the CTF case
// ---------------------------------------------------------------------------

TEST_CASE("SymbolicExecutor - recovers the key of an XOR crackme",
          "[ignil][symbolic][ctf]") {
  // The classic shape: the flag buffer is XORed with a constant and compared
  // against a hardcoded blob. Symbolise the buffer, assert the comparison
  // succeeded, and read the flag out of the model.
  constexpr std::uint64_t kBuffer = 0x4000;
  constexpr std::uint64_t kKey = 0x5A5A5A5A5A5A5A5Aull;
  const char *secret = "DRAGON!!";

  std::uint64_t expected = 0;
  std::memcpy(&expected, secret, 8);
  expected ^= kKey;

  Block block{kAddr};
  Temp addr{0, 64};
  Temp loaded{1, 64};
  block.append({Opcode::COPY, Value{addr}, {Value{Const{kBuffer, 64}}}, kAddr});
  block.append({Opcode::LOAD, Value{loaded}, {Value{addr}}, kAddr});
  block.append({Opcode::XOR, Value{x86::RAX},
                {Value{loaded}, Value{Const{kKey, 64}}}, kAddr});

  SymbolicExecutor exec;
  exec.symbolizeMemory(kBuffer, 8, "flag");
  REQUIRE(exec.run(block).ok());

  auto solution = exec.solve(exec.readRegister(x86::RAX) == exec.bv(expected, 64));
  REQUIRE(solution.has_value());

  const std::vector<std::uint8_t> flag = solution->bytes("flag");
  REQUIRE(flag.size() == 8);
  REQUIRE(std::string(flag.begin(), flag.end()) == secret);
}

TEST_CASE("SymbolicExecutor - inverts an FNV-1a hash under a charset "
          "constraint",
          "[ignil][symbolic][ctf]") {
  // A "check(input) == magic" challenge: four bytes of input are folded into
  // an FNV-1a digest. There is no closed form to invert, so the solver has to
  // search - which is the point.
  constexpr std::uint64_t kBuffer = 0x5000;
  constexpr std::uint32_t kBasis = 0x811C9DC5u;
  constexpr std::uint32_t kPrime = 0x01000193u;
  constexpr std::size_t kLen = 4;

  const auto fnv1a = [](const std::vector<std::uint8_t> &data) {
    std::uint32_t h = kBasis;
    for (std::uint8_t b : data) {
      h ^= b;
      h *= kPrime;
    }
    return h;
  };

  const std::vector<std::uint8_t> target = {'l', '3', '3', 't'};
  const std::uint32_t digest = fnv1a(target);

  // h = basis; for each byte: h = (h ^ byte) * prime
  Block block{kAddr};
  block.append({Opcode::COPY, Value{x86::EAX}, {Value{Const{kBasis, 32}}},
                kAddr});
  std::uint32_t temp = 0;
  for (std::size_t i = 0; i < kLen; ++i) {
    Temp byteAddr{temp++, 64};
    Temp byte{temp++, 8};
    Temp wide{temp++, 32};
    Temp mixed{temp++, 32};

    block.append({Opcode::COPY, Value{byteAddr},
                  {Value{Const{kBuffer + i, 64}}}, kAddr});
    block.append({Opcode::LOAD, Value{byte}, {Value{byteAddr}}, kAddr});
    block.append({Opcode::ZEXT, Value{wide}, {Value{byte}}, kAddr});
    block.append({Opcode::XOR, Value{mixed}, {Value{x86::EAX}, Value{wide}},
                  kAddr});
    block.append({Opcode::MUL, Value{x86::EAX},
                  {Value{mixed}, Value{Const{kPrime, 32}}}, kAddr});
  }

  SymbolicExecutor exec;
  exec.symbolizeMemory(kBuffer, kLen, "input");

  // Printable ASCII only, or the solver is free to return any of the many
  // preimages a 32-bit digest has.
  for (std::size_t i = 0; i < kLen; ++i) {
    const z3::expr b = exec.readMemory(kBuffer + i, 8);
    exec.assume(z3::uge(b, exec.bv(0x21, 8)));
    exec.assume(z3::ule(b, exec.bv(0x7E, 8)));
  }

  REQUIRE(exec.run(block).ok());

  auto solution = exec.solve(exec.readRegister(x86::EAX) == exec.bv(digest, 32));
  REQUIRE(solution.has_value());

  // The solver may legitimately find a different preimage than the one the
  // digest came from - what has to hold is that it hashes to the target.
  const std::vector<std::uint8_t> recovered = solution->bytes("input");
  REQUIRE(recovered.size() == kLen);
  REQUIRE(fnv1a(recovered) == digest);
  for (std::uint8_t b : recovered)
    REQUIRE((b >= 0x21 && b <= 0x7E));
}

TEST_CASE("SymbolicExecutor - reports an unsatisfiable goal instead of "
          "inventing an answer",
          "[ignil][symbolic][ctf]") {
  // RAX = RDI * 2 can never be odd.
  Block block{kAddr};
  block.append({Opcode::MUL, Value{x86::RAX},
                {Value{x86::RDI}, Value{Const{2, 64}}}, kAddr});

  SymbolicExecutor exec;
  exec.symbolizeRegister(x86::RDI, "x");
  REQUIRE(exec.run(block).ok());

  REQUIRE_FALSE(
          exec.solve(exec.readRegister(x86::RAX) == exec.bv(7, 64)).has_value());
  REQUIRE(exec.solve(exec.readRegister(x86::RAX) == exec.bv(8, 64)).has_value());
}

// ---------------------------------------------------------------------------
// Interaction with the IGNIL optimisation passes
// ---------------------------------------------------------------------------

TEST_CASE("SymbolicExecutor - simplifying a block first does not change what "
          "it computes",
          "[ignil][symbolic][simplify]") {
  // Deliberately full of things simplifyBlock will fold away: an identity
  // add, a redundant copy chain, an annihilating AND, and a dead temp.
  Block block{kAddr};
  Temp t0{0, 64};
  Temp t1{1, 64};
  Temp t2{2, 64};
  Temp dead{3, 64};
  block.append({Opcode::COPY, Value{t0}, {Value{x86::RDI}}, kAddr});
  block.append({Opcode::ADD, Value{t1}, {Value{t0}, Value{Const{0, 64}}},
                kAddr});
  block.append({Opcode::AND, Value{dead}, {Value{t1}, Value{Const{0, 64}}},
                kAddr});
  block.append({Opcode::MUL, Value{t2}, {Value{t1}, Value{Const{3, 64}}},
                kAddr});
  block.append({Opcode::ADD, Value{x86::RAX}, {Value{t2}, Value{Const{7, 64}}},
                kAddr});

  dragon::ignil::SimplifyStats stats;
  const Block folded = simplifyBlock(block, stats);
  REQUIRE(stats.changed());
  REQUIRE(folded.size() < block.size());

  const auto resultOf = [](const Block &b) {
    SymbolicExecutor exec;
    exec.symbolizeRegister(x86::RDI, "x");
    REQUIRE(exec.run(b).ok());
    // 3x + 7, whichever block computed it.
    const z3::expr x = exec.state().symbols().at("x");
    return exec.proves(exec.readRegister(x86::RAX) ==
                       x * exec.bv(3, 64) + exec.bv(7, 64));
  };

  REQUIRE(resultOf(block) == true);
  REQUIRE(resultOf(folded) == true);
}

// ---------------------------------------------------------------------------
// Malformed input
// ---------------------------------------------------------------------------

TEST_CASE("SymbolicExecutor - rejects malformed ops rather than guessing",
          "[ignil][symbolic]") {
  SECTION("binary op with one source") {
    Block block{kAddr};
    block.append({Opcode::ADD, Value{x86::RAX}, {Value{Const{1, 64}}}, kAddr});

    SymbolicExecutor exec;
    RunResult r = exec.run(block);
    REQUIRE(r.status == ExecStatus::MalformedOp);
    REQUIRE(r.exit == dragon::ignil::ExitKind::Error);
    REQUIRE(r.error.find("ADD") != std::string::npos);
  }

  SECTION("arithmetic with no destination") {
    Block block{kAddr};
    block.append({Opcode::ADD, std::nullopt,
                  {Value{Const{1, 64}}, Value{Const{2, 64}}}, kAddr});

    SymbolicExecutor exec;
    REQUIRE(exec.run(block).status == ExecStatus::MalformedOp);
  }
}
