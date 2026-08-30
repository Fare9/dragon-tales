#include <catch2/catch_test_macros.hpp>

#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>
#include <dragon/lifters/ignil/SymbolicExecutor.hpp>
#include <dragon/lifters/ignil/x86/x86IGNILRegs.hpp>

#include <algorithm>
#include <vector>

namespace x86 = dragon::x86;
using dragon::ignil::Block;
using dragon::ignil::Const;
using dragon::ignil::ExitKind;
using dragon::ignil::Opcode;
using dragon::ignil::RunResult;
using dragon::ignil::Successor;
using dragon::ignil::SymbolicExecutor;
using dragon::ignil::SymbolicOptions;
using dragon::ignil::Temp;
using dragon::ignil::Value;

namespace {

constexpr std::uint64_t kAddr = 0x1000;

std::vector<std::uint64_t> successorAddresses(const RunResult &r) {
  std::vector<std::uint64_t> out;
  out.reserve(r.successors.size());
  for (const Successor &s : r.successors)
    out.push_back(s.address);
  std::sort(out.begin(), out.end());
  return out;
}

/// Little-endian encoding of a 64-bit jump-table entry.
void appendPointer(std::vector<std::uint8_t> &table, std::uint64_t value) {
  for (int i = 0; i < 8; ++i)
    table.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
}

} // namespace

// ---------------------------------------------------------------------------
// Conditional branches
// ---------------------------------------------------------------------------

TEST_CASE("SymbolicExecutor - a symbolic condition keeps both edges",
          "[ignil][symbolic][cfg]") {
  // if (RDI == 0) goto 0x2000 else goto 0x3000
  Block block{kAddr};
  Temp cond{0, 1};
  block.append({Opcode::EQ, Value{cond},
                {Value{x86::RDI}, Value{Const{0, 64}}}, kAddr});
  block.append({Opcode::CJMP, std::nullopt,
                {Value{cond}, Value{Const{0x2000, 64}},
                 Value{Const{0x3000, 64}}},
                kAddr + 4});

  SymbolicExecutor exec;
  exec.symbolizeRegister(x86::RDI, "x");

  RunResult r = exec.run(block);
  REQUIRE(r.complete());
  REQUIRE(r.exit == ExitKind::ConditionalJump);
  REQUIRE(successorAddresses(r) == std::vector<std::uint64_t>{0x2000, 0x3000});
}

TEST_CASE("SymbolicExecutor - path constraints prune an unreachable edge",
          "[ignil][symbolic][cfg]") {
  // The same branch, but the caller already knows RDI > 10.
  Block block{kAddr};
  Temp cond{0, 1};
  block.append({Opcode::EQ, Value{cond},
                {Value{x86::RDI}, Value{Const{0, 64}}}, kAddr});
  block.append({Opcode::CJMP, std::nullopt,
                {Value{cond}, Value{Const{0x2000, 64}},
                 Value{Const{0x3000, 64}}},
                kAddr + 4});

  SymbolicExecutor exec;
  const z3::expr x = exec.symbolizeRegister(x86::RDI, "x");
  exec.assume(z3::ugt(x, exec.bv(10, 64)));

  RunResult r = exec.run(block);
  REQUIRE(r.complete());
  REQUIRE(r.successors.size() == 1);
  REQUIRE(r.successors[0].address == 0x3000);
  REQUIRE_FALSE(r.successors[0].taken);
}

TEST_CASE("SymbolicExecutor - an opaque predicate collapses to one edge",
          "[ignil][symbolic][cfg]") {
  // x * (x + 1) is always even, so the low bit is always zero and the branch
  // is not really a branch. No pattern matching is involved: the solver
  // simply cannot satisfy the other side.
  Block block{kAddr};
  Temp plusOne{0, 64};
  Temp product{1, 64};
  Temp lowBit{2, 64};
  Temp isEven{3, 1};

  block.append({Opcode::ADD, Value{plusOne},
                {Value{x86::RDI}, Value{Const{1, 64}}}, kAddr});
  block.append({Opcode::MUL, Value{product},
                {Value{x86::RDI}, Value{plusOne}}, kAddr});
  block.append({Opcode::AND, Value{lowBit},
                {Value{product}, Value{Const{1, 64}}}, kAddr});
  block.append({Opcode::EQ, Value{isEven},
                {Value{lowBit}, Value{Const{0, 64}}}, kAddr});
  block.append({Opcode::CJMP, std::nullopt,
                {Value{isEven}, Value{Const{0x2000, 64}},
                 Value{Const{0xDEAD, 64}}},
                kAddr + 4});

  SymbolicExecutor exec;
  exec.symbolizeRegister(x86::RDI, "x");

  RunResult r = exec.run(block);
  REQUIRE(r.complete());
  REQUIRE(r.successors.size() == 1);
  REQUIRE(r.successors[0].address == 0x2000);
  REQUIRE(r.successors[0].taken);
}

// ---------------------------------------------------------------------------
// Computed jumps - jump tables and VM dispatch
// ---------------------------------------------------------------------------

TEST_CASE("SymbolicExecutor - recovers every target of a jump table",
          "[ignil][symbolic][cfg]") {
  // jmp [table + idx*8], with idx bounded the way a preceding range check
  // would bound it.
  constexpr std::uint64_t kTable = 0x5000;
  const std::vector<std::uint64_t> targets = {0x1100, 0x1200, 0x1300, 0x1400};

  std::vector<std::uint8_t> blob;
  for (std::uint64_t t : targets)
    appendPointer(blob, t);

  Block block{kAddr};
  Temp scaled{0, 64};
  Temp slot{1, 64};
  Temp target{2, 64};
  block.append({Opcode::MUL, Value{scaled},
                {Value{x86::RDI}, Value{Const{8, 64}}}, kAddr});
  block.append({Opcode::ADD, Value{slot},
                {Value{scaled}, Value{Const{kTable, 64}}}, kAddr});
  block.append({Opcode::LOAD, Value{target}, {Value{slot}}, kAddr});
  block.append({Opcode::JMP, std::nullopt, {Value{target}}, kAddr + 4});

  SymbolicExecutor exec;
  exec.mapMemory(kTable, blob);
  const z3::expr idx = exec.symbolizeRegister(x86::RDI, "idx");
  exec.assume(z3::ult(idx, exec.bv(targets.size(), 64)));

  RunResult r = exec.run(block);
  REQUIRE(r.complete());
  REQUIRE(r.exit == ExitKind::Jump);
  REQUIRE(successorAddresses(r) == targets);
}

TEST_CASE("SymbolicExecutor - walks a VM dispatch loop one handler at a time",
          "[ignil][symbolic][cfg]") {
  // The shape a bytecode VM's dispatcher has: fetch an opcode byte from the
  // bytecode stream, look the handler up in a table, jump to it. Executing
  // the dispatcher once with the opcode symbolic yields every handler it can
  // reach - which is the edge set a CFG reconstructor is missing.
  constexpr std::uint64_t kHandlers = 0x6000;
  constexpr std::uint64_t kBytecode = 0x7000;
  const std::vector<std::uint64_t> handlers = {0xAA00, 0xBB00, 0xCC00};

  std::vector<std::uint8_t> table;
  for (std::uint64_t h : handlers)
    appendPointer(table, h);

  // RSI holds the VM instruction pointer.
  Block dispatcher{kAddr};
  Temp opcode{0, 8};
  Temp wide{1, 64};
  Temp scaled{2, 64};
  Temp slot{3, 64};
  Temp handler{4, 64};
  dispatcher.append({Opcode::LOAD, Value{opcode}, {Value{x86::RSI}}, kAddr});
  dispatcher.append({Opcode::ZEXT, Value{wide}, {Value{opcode}}, kAddr});
  dispatcher.append({Opcode::MUL, Value{scaled},
                     {Value{wide}, Value{Const{8, 64}}}, kAddr});
  dispatcher.append({Opcode::ADD, Value{slot},
                     {Value{scaled}, Value{Const{kHandlers, 64}}}, kAddr});
  dispatcher.append({Opcode::LOAD, Value{handler}, {Value{slot}}, kAddr});
  dispatcher.append({Opcode::JMP, std::nullopt, {Value{handler}}, kAddr + 4});

  SymbolicExecutor exec;
  exec.mapMemory(kHandlers, table);
  exec.setRegister(x86::RSI, kBytecode);

  // The bytecode byte is unknown, but a well-formed program only ever holds a
  // valid opcode there.
  const z3::expr op = exec.symbolizeMemory(kBytecode, 1, "vm_opcode");
  exec.assume(z3::ult(op, exec.bv(handlers.size(), 8)));

  RunResult r = exec.run(dispatcher);
  REQUIRE(r.complete());
  REQUIRE(successorAddresses(r) == handlers);

  // Each edge carries the condition that reaches it, so a caller can pin the
  // state down before descending into one handler.
  for (const Successor &s : r.successors) {
    const auto it = std::find(handlers.begin(), handlers.end(), s.address);
    REQUIRE(it != handlers.end());
    const std::uint64_t expectedOpcode =
            static_cast<std::uint64_t>(it - handlers.begin());
    REQUIRE(exec.proves(z3::implies(s.condition,
                                    op == exec.bv(expectedOpcode, 8))) == true);
  }
}

TEST_CASE("SymbolicExecutor - an unbounded computed jump is reported as "
          "unresolved",
          "[ignil][symbolic][cfg]") {
  // Without the range check, the handler index is unconstrained and the
  // target could be anything. Saying so is the only honest answer.
  Block block{kAddr};
  block.append({Opcode::JMP, std::nullopt, {Value{x86::RDI}}, kAddr});

  SymbolicExecutor exec;
  exec.symbolizeRegister(x86::RDI, "target");

  RunResult r = exec.run(block);
  REQUIRE(r.ok());
  REQUIRE_FALSE(r.complete());
  REQUIRE(r.successors.empty());
  REQUIRE(r.warnings.size() == 1);
}

TEST_CASE("SymbolicExecutor - the branch-target budget is configurable",
          "[ignil][symbolic][cfg]") {
  Block block{kAddr};
  block.append({Opcode::JMP, std::nullopt, {Value{x86::RDI}}, kAddr});

  const auto runWithBudget = [&](std::size_t budget) {
    SymbolicOptions opts;
    opts.maxBranchTargets = budget;
    SymbolicExecutor exec{opts};
    const z3::expr t = exec.symbolizeRegister(x86::RDI, "target");
    exec.assume(z3::uge(t, exec.bv(0x2000, 64)));
    exec.assume(z3::ult(t, exec.bv(0x2005, 64))); // exactly 5 targets
    return exec.run(block);
  };

  RunResult tooSmall = runWithBudget(4);
  REQUIRE_FALSE(tooSmall.complete());
  REQUIRE(tooSmall.successors.empty());

  // A budget that exactly fits still reports the set as complete.
  RunResult exact = runWithBudget(5);
  REQUIRE(exact.complete());
  REQUIRE(exact.successors.size() == 5);
}

// ---------------------------------------------------------------------------
// Traces
// ---------------------------------------------------------------------------

TEST_CASE("SymbolicExecutor - a trace runs through its interior terminators",
          "[ignil][symbolic][cfg]") {
  // liftTrace() splices consecutive basic blocks into one Block, so the JMP
  // in the middle is not the end of anything.
  Block trace{kAddr};
  trace.append({Opcode::COPY, Value{x86::RAX}, {Value{Const{1, 64}}}, kAddr});
  trace.append({Opcode::JMP, std::nullopt, {Value{Const{0x2000, 64}}}, kAddr});
  trace.append({Opcode::ADD, Value{x86::RAX},
                {Value{x86::RAX}, Value{Const{41, 64}}}, 0x2000});
  trace.append({Opcode::RET, std::nullopt, {}, 0x2004});

  SECTION("stopping at the first terminator is the block default") {
    SymbolicExecutor exec;
    RunResult r = exec.run(trace);
    REQUIRE(r.exit == ExitKind::Jump);
    REQUIRE(r.stats.opsExecuted == 2);
    REQUIRE(exec.evaluate(exec.readRegister(x86::RAX)) == 1);
  }

  SECTION("trace mode steps over it") {
    SymbolicOptions opts;
    opts.stopAtTerminator = false;
    SymbolicExecutor exec{opts};
    RunResult r = exec.run(trace);
    REQUIRE(r.stats.opsExecuted == 4);
    REQUIRE(r.exit == ExitKind::Return);
    REQUIRE(exec.evaluate(exec.readRegister(x86::RAX)) == 42);
    // The interior jump is still reported.
    REQUIRE(successorAddresses(r) == std::vector<std::uint64_t>{0x2000});
  }
}

// ---------------------------------------------------------------------------
// End to end: real machine code, disassembled and lifted
// ---------------------------------------------------------------------------

TEST_CASE("SymbolicExecutor - runs a block lifted from x86-64 machine code",
          "[ignil][symbolic][x86_64]") {
  // 0x1000: 48 31 F8   xor rax, rdi
  // 0x1003: 48 01 F0   add rax, rsi
  // 0x1006: c3         ret
  const std::vector<std::uint8_t> bytes = {
          0x48, 0x31, 0xF8, // xor rax, rdi
          0x48, 0x01, 0xF0, // add rax, rsi
          0xC3, // ret
  };

  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;
  dragon::Disassembler dis{dragon::Architecture::X86_64, cfg};
  dragon::Graph g{dragon::Architecture::X86_64, dragon::Configuration{}};
  dis.disassemble(bytes, kAddr, g);

  const Block *lifted = g.getIGNILBlock(kAddr);
  REQUIRE(lifted != nullptr);
  REQUIRE_FALSE(lifted->empty());

  SECTION("the block's meaning is recovered symbolically") {
    SymbolicExecutor exec;
    exec.setRegister(x86::RAX, 0);
    const z3::expr rdi = exec.symbolizeRegister(x86::RDI, "a");
    const z3::expr rsi = exec.symbolizeRegister(x86::RSI, "b");

    RunResult r = exec.run(*lifted);
    REQUIRE(r.ok());
    REQUIRE(r.exit == ExitKind::Return);

    // (0 ^ a) + b
    REQUIRE(exec.proves(exec.readRegister(x86::RAX) == rdi + rsi) == true);
  }

  SECTION("and can be inverted for an argument") {
    SymbolicExecutor exec;
    exec.setRegister(x86::RAX, 0);
    exec.symbolizeRegister(x86::RDI, "a");
    exec.setRegister(x86::RSI, 0x37);
    REQUIRE(exec.run(*lifted).ok());

    auto solution =
            exec.solve(exec.readRegister(x86::RAX) == exec.bv(0x1337, 64));
    REQUIRE(solution.has_value());
    REQUIRE(solution->value("a") == 0x1337 - 0x37);
  }
}

TEST_CASE("SymbolicExecutor - resolves a conditional branch lifted from "
          "x86-64 machine code",
          "[ignil][symbolic][x86_64][cfg]") {
  // 0x1000: 48 83 FF 0A   cmp rdi, 10
  // 0x1004: 7C 03         jl 0x1009
  // 0x1006: 48 FF C0      inc rax
  // 0x1009: c3            ret
  const std::vector<std::uint8_t> bytes = {
          0x48, 0x83, 0xFF, 0x0A, // cmp rdi, 10
          0x7C, 0x03, // jl 0x1009
          0x48, 0xFF, 0xC0, // inc rax
          0xC3, // ret
  };

  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;
  dragon::Disassembler dis{dragon::Architecture::X86_64, cfg};
  dragon::Graph g{dragon::Architecture::X86_64, dragon::Configuration{}};
  dis.disassemble(bytes, kAddr, g);

  const Block *entry = g.getIGNILBlock(kAddr);
  REQUIRE(entry != nullptr);

  SECTION("an unknown argument leaves both edges live") {
    SymbolicExecutor exec;
    exec.symbolizeRegister(x86::RDI, "n");

    RunResult r = exec.run(*entry);
    REQUIRE(r.complete());
    REQUIRE(r.exit == ExitKind::ConditionalJump);
    REQUIRE(successorAddresses(r) == std::vector<std::uint64_t>{0x1006, 0x1009});
  }

  SECTION("knowing the argument decides the branch") {
    SymbolicExecutor exec;
    const z3::expr n = exec.symbolizeRegister(x86::RDI, "n");
    exec.assume(n > exec.bv(100, 64)); // signed: the jl is not taken

    RunResult r = exec.run(*entry);
    REQUIRE(r.complete());
    REQUIRE(r.successors.size() == 1);
    REQUIRE(r.successors[0].address == 0x1006);
  }
}
