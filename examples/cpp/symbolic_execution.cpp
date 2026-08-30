/// symbolic_execution.cpp - dragon SymbolicExecutor demo
///
/// Executes lifted IGNIL symbolically over Z3 bitvectors and asks the solver
/// about the result. Four things this is good for:
///
///   1. Reading a block's meaning back out, through the flag-setting noise.
///   2. Recovering the input that makes a check pass (the CTF case).
///   3. Deciding which edges of a branch are real.
///   4. Resolving a computed jump to the concrete addresses it can reach,
///      which is what a CFG reconstructor is missing on a virtualised binary.
///
/// Every block here is real x86-64 machine code, disassembled and lifted the
/// same way any other dragon analysis gets one.
///
/// Build with -DDRAGON_USE_Z3=ON (the default) and run:
///     ./build/bin/symbolic_execution

#include <dragon/Configuration.hpp>
#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>
#include <dragon/lifters/ignil/SymbolicExecutor.hpp>
#include <dragon/lifters/ignil/x86/x86IGNILRegs.hpp>

#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace ignil = dragon::ignil;
namespace x86 = dragon::x86;

namespace {

void heading(const std::string &title) {
  std::cout << "\n" << std::string(72, '=') << "\n"
            << title << "\n"
            << std::string(72, '=') << "\n";
}

std::string yesNo(std::optional<bool> v) {
  if (!v)
    return "unknown (solver gave up)";
  return *v ? "true" : "false";
}

/// Disassemble, lift, and hand back the IGNIL block at `base`. The Graph owns
/// the block, so it is returned alongside it.
struct Lifted {
  dragon::Graph graph;
  const ignil::Block *block;
};

Lifted lift(const std::vector<std::uint8_t> &code, std::uint64_t base = 0x1000) {
  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;

  dragon::Disassembler dis{dragon::Architecture::X86_64, cfg};
  dragon::Graph graph{dragon::Architecture::X86_64, dragon::Configuration{}};
  dis.disassemble(code, base, graph);

  const ignil::Block *block = graph.getIGNILBlock(base);
  return Lifted{std::move(graph), block};
}

// ---------------------------------------------------------------------------
// 1. Reading a block's meaning back out
// ---------------------------------------------------------------------------

void demoExpressionRecovery() {
  heading("1. What does this block actually compute?");

  //   0x1000: 48 31 F8   xor rax, rdi
  //   0x1003: 48 01 F0   add rax, rsi
  //   0x1006: c3         ret
  Lifted lifted = lift({0x48, 0x31, 0xF8, 0x48, 0x01, 0xF0, 0xC3});

  ignil::SymbolicExecutor exec;
  exec.setRegister(x86::RAX, 0);
  const z3::expr a = exec.symbolizeRegister(x86::RDI, "a");
  const z3::expr b = exec.symbolizeRegister(x86::RSI, "b");

  const ignil::RunResult result = exec.run(*lifted.block);
  std::cout << "  " << lifted.block->size() << " IGNIL ops, "
            << result.stats.opsExecuted << " executed\n";

  const z3::expr rax = exec.readRegister(x86::RAX);
  // The flags the lifter sets on the way do not get in the way: the solver is
  // asked about the value, not the syntax.
  std::cout << "  proves RAX == a + b : " << yesNo(exec.proves(rax == a + b))
            << "\n"
            << "  proves RAX == a * b : " << yesNo(exec.proves(rax == a * b))
            << "\n";

  // Pin one argument down and the other is solvable.
  exec.assume(b == exec.bv(0x37, 64));
  if (auto solution = exec.solve(rax == exec.bv(0x1337, 64)))
    std::cout << "  a such that RAX == 0x1337 with b == 0x37: 0x" << std::hex
              << solution->value("a").value_or(0) << std::dec << "\n";
}

// ---------------------------------------------------------------------------
// 2. Inverting a check - the CTF case
// ---------------------------------------------------------------------------

void demoCrackme() {
  heading("2. Recovering a flag from an XOR check");

  constexpr std::uint64_t kBuffer = 0x4000;
  constexpr std::uint64_t kKey = 0x5A5A5A5A5A5A5A5Aull;
  const char *secret = "DRAGON!!";

  std::uint64_t expected = 0;
  std::memcpy(&expected, secret, 8);
  expected ^= kKey;

  //   0x1000: 48 8B 07   mov rax, [rdi]
  //   0x1003: 48 31 F0   xor rax, rsi
  //   0x1006: c3         ret
  Lifted lifted = lift({0x48, 0x8B, 0x07, 0x48, 0x31, 0xF0, 0xC3});

  ignil::SymbolicExecutor exec;
  exec.setRegister(x86::RDI, kBuffer);
  exec.setRegister(x86::RSI, kKey);
  exec.symbolizeMemory(kBuffer, 8, "flag");
  exec.run(*lifted.block);

  const z3::expr rax = exec.readRegister(x86::RAX);
  auto solution = exec.solve(rax == exec.bv(expected, 64));
  if (!solution) {
    std::cout << "  no input satisfies the check\n";
    return;
  }

  const std::vector<std::uint8_t> flag = solution->bytes("flag");
  std::cout << "  target    = 0x" << std::hex << std::setw(16)
            << std::setfill('0') << expected << std::dec << std::setfill(' ')
            << "\n"
            << "  recovered = \"" << std::string(flag.begin(), flag.end())
            << "\"\n";

  // A goal with no model is reported as such rather than guessed at.
  std::cout << "  unsatisfiable goal solves: "
            << (exec.solve(rax != rax) ? "yes" : "no") << "\n";
}

// ---------------------------------------------------------------------------
// 3. Deciding a branch
// ---------------------------------------------------------------------------

void printSuccessors(const ignil::RunResult &r) {
  for (const ignil::Successor &s : r.successors)
    std::cout << "    -> 0x" << std::hex << s.address << std::dec
              << "  (taken=" << (s.taken ? "true" : "false") << ")\n";
  std::cout << "    complete=" << (r.complete() ? "true" : "false")
            << ", solver queries=" << r.stats.solverQueries << "\n";
}

void demoBranchResolution() {
  heading("3. Which edges of this branch are real?");

  //   0x1000: 48 83 FF 0A   cmp rdi, 10
  //   0x1004: 7C 03         jl 0x1009
  //   0x1006: 48 FF C0      inc rax
  //   0x1009: c3            ret
  const std::vector<std::uint8_t> code = {0x48, 0x83, 0xFF, 0x0A, 0x7C,
                                          0x03, 0x48, 0xFF, 0xC0, 0xC3};

  {
    std::cout << "  with RDI unknown:\n";
    Lifted lifted = lift(code);
    ignil::SymbolicExecutor exec;
    exec.symbolizeRegister(x86::RDI, "n");
    printSuccessors(exec.run(*lifted.block));
  }
  {
    std::cout << "  knowing RDI > 100:\n";
    Lifted lifted = lift(code);
    ignil::SymbolicExecutor exec;
    const z3::expr n = exec.symbolizeRegister(x86::RDI, "n");
    exec.assume(n > exec.bv(100, 64));
    printSuccessors(exec.run(*lifted.block));
  }
}

// ---------------------------------------------------------------------------
// 4. Resolving a dispatch table - the VM case
// ---------------------------------------------------------------------------

void demoDispatchRecovery() {
  heading("4. Where can this computed jump go?");

  constexpr std::uint64_t kHandlers = 0x6000;
  constexpr std::uint64_t kBytecode = 0x7000;
  const std::vector<std::uint64_t> handlers = {0xAA00, 0xBB00, 0xCC00};

  std::vector<std::uint8_t> table;
  for (std::uint64_t h : handlers)
    for (int i = 0; i < 8; ++i)
      table.push_back(static_cast<std::uint8_t>((h >> (i * 8)) & 0xFF));

  // The shape a bytecode VM's dispatcher has: fetch the opcode, index the
  // handler table with it, jump there.
  //   0x1000: 0F B6 37                movzx esi, byte ptr [rdi]
  //   0x1003: FF 24 F5 00 60 00 00    jmp qword ptr [rsi*8 + 0x6000]
  const std::vector<std::uint8_t> code = {0x0F, 0xB6, 0x37, 0xFF, 0x24,
                                          0xF5, 0x00, 0x60, 0x00, 0x00};

  {
    std::cout << "  with the opcode range-checked:\n";
    Lifted lifted = lift(code);
    ignil::SymbolicExecutor exec;
    exec.mapMemory(kHandlers, table);
    exec.setRegister(x86::RDI, kBytecode);

    // A well-formed program only ever holds a valid opcode there.
    const z3::expr opcode = exec.symbolizeMemory(kBytecode, 1, "vm_opcode");
    exec.assume(z3::ult(opcode, exec.bv(handlers.size(), 8)));

    const ignil::RunResult r = exec.run(*lifted.block);
    std::cout << "    discovered " << r.successors.size() << " handlers "
              << "(complete=" << (r.complete() ? "true" : "false") << "):\n";
    for (const ignil::Successor &s : r.successors)
      std::cout << "      -> 0x" << std::hex << s.address << std::dec << "\n";
  }
  {
    std::cout << "  without it, the target is unbounded:\n";
    Lifted lifted = lift(code);
    ignil::SymbolicExecutor exec;
    exec.mapMemory(kHandlers, table);
    exec.setRegister(x86::RDI, kBytecode);
    exec.symbolizeMemory(kBytecode, 1, "vm_opcode");

    const ignil::RunResult r = exec.run(*lifted.block);
    std::cout << "    successors=" << r.successors.size()
              << ", complete=" << (r.complete() ? "true" : "false") << "\n";
    for (const ignil::ExecWarning &w : r.warnings)
      std::cout << "    warning @0x" << std::hex << w.address << std::dec
                << ": " << w.message << "\n";
  }
}

// ---------------------------------------------------------------------------
// 5. Proving an obfuscated expression is something simpler
// ---------------------------------------------------------------------------

void demoMBA() {
  heading("5. Seeing through mixed boolean-arithmetic");

  //   rax = (rdi | rsi) + (rdi & rsi), which is just rdi + rsi
  //   0x1000: 48 89 F8   mov rax, rdi     0x1009: 48 21 F1   and rcx, rsi
  //   0x1003: 48 09 F0   or  rax, rsi     0x100C: 48 01 C8   add rax, rcx
  //   0x1006: 48 89 F9   mov rcx, rdi     0x100F: c3         ret
  {
    Lifted lifted = lift({0x48, 0x89, 0xF8, 0x48, 0x09, 0xF0, 0x48, 0x89,
                          0xF9, 0x48, 0x21, 0xF1, 0x48, 0x01, 0xC8, 0xC3});

    ignil::SymbolicExecutor exec;
    const z3::expr a = exec.symbolizeRegister(x86::RDI, "a");
    const z3::expr b = exec.symbolizeRegister(x86::RSI, "b");
    exec.run(*lifted.block);

    const z3::expr rax = exec.readRegister(x86::RAX);
    std::cout << "  (a|b) + (a&b) == a + b : "
              << yesNo(exec.proves(rax == a + b)) << "\n"
              << "  (a|b) + (a&b) == a ^ b : "
              << yesNo(exec.proves(rax == (a ^ b))) << "\n";
  }

  // A block whose result does not depend on its input at all:
  //   rcx = ((x ^ k) + k) - (x ^ k)  ==  k, for every x.
  //   0x1000: 48 89 F8   mov rax, rdi     0x1009: 48 01 F1   add rcx, rsi
  //   0x1003: 48 31 F0   xor rax, rsi     0x100C: 48 29 C1   sub rcx, rax
  //   0x1006: 48 89 C1   mov rcx, rax     0x100F: c3         ret
  {
    Lifted lifted = lift({0x48, 0x89, 0xF8, 0x48, 0x31, 0xF0, 0x48, 0x89,
                          0xC1, 0x48, 0x01, 0xF1, 0x48, 0x29, 0xC1, 0xC3});

    ignil::SymbolicExecutor exec;
    exec.symbolizeRegister(x86::RDI, "x");
    exec.setRegister(x86::RSI, 0xA5A5);
    exec.run(*lifted.block);

    const auto constant = exec.evaluate(exec.readRegister(x86::RCX));
    const auto varying = exec.evaluate(exec.readRegister(x86::RAX));
    std::cout << "  x-independent result   : ";
    if (constant)
      std::cout << "0x" << std::hex << *constant << std::dec << "\n";
    else
      std::cout << "not constant\n";
    std::cout << "  x-dependent result     : "
              << (varying ? "constant" : "not constant") << "\n";
  }
}

} // namespace

int main() {
  demoExpressionRecovery();
  demoCrackme();
  demoBranchResolution();
  demoDispatchRecovery();
  demoMBA();
  std::cout << "\n";
  return 0;
}
