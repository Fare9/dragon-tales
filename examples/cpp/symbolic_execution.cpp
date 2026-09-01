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
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
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

// ---------------------------------------------------------------------------
// Showing the work: IGNIL in, Z3 out
// ---------------------------------------------------------------------------

/// Indent every line of a multi-line string, so a block listing or a Z3
/// pretty-printed expression nests under the demo output around it.
std::string indented(const std::string &text, const std::string &pad) {
  std::string out;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);)
    out += pad + line + "\n";
  return out;
}

/// A pathological expression should not bury the demo it belongs to.
std::string clipped(const std::string &s, std::size_t max = 640) {
  if (s.size() <= max)
    return s;
  return s.substr(0, max) + "\n... [" + std::to_string(s.size() - max) +
         " more chars]";
}

/// Collapse a pretty-printed expression onto one line, for the places it
/// belongs inline in a listing rather than in a block of its own.
std::string oneLine(const z3::expr &e, std::size_t max = 180) {
  const std::string s = e.to_string();
  std::string out;
  bool space = false;
  for (const char c : s) {
    const bool blank = c == '\n' || c == ' ' || c == '\t';
    if (blank) {
      space = true;
      continue;
    }
    if (space && !out.empty())
      out += ' ';
    space = false;
    out += c;
  }
  if (out.size() > max)
    out = out.substr(0, max) + " ...";
  return out;
}

/// What the lifter handed the executor.
///
/// IGNIL is a flat op list, not a tree: one `Op` per elementary effect, with
/// every side effect the machine instruction implies - the flags above all -
/// spelled out explicitly. That flatness is why the executor is a plain `for`
/// loop over `ops()` rather than a recursive tree walk, and it is why three
/// x86 instructions become the op counts below.
void showIGNIL(const ignil::Block &block) {
  std::cout << "\n  -- IGNIL: what the executor walks --\n"
            << indented(ignil::toString(block), "  ");
}

/// What the executor built out of it.
///
/// The first form is literally the `z3::expr` the run produced: one Z3 node per
/// IGNIL op that fed this value, threaded through the byte-granular register
/// file, with every extract/concat that implies still in place. The second is
/// Z3's rewriter run over the same term. `simplify()` is syntactic
/// normalisation, *not* a solver call - it folds constants and collapses
/// extract-of-concat, but it proves nothing. Anything that needs a proof (an
/// MBA identity, say) comes through it unchanged, and only `proves()` settles
/// it. Demo 5 is that difference on purpose.
void showExpr(std::string_view label, const z3::expr &e) {
  const std::string raw = e.to_string();
  const std::string simple = e.simplify().to_string();

  std::cout << "\n  -- Z3: " << label << " --\n"
            << "  as executed (" << raw.size() << " chars):\n"
            << indented(clipped(raw), "    ");
  if (simple == raw) {
    std::cout << "  simplify() leaves it unchanged\n";
    return;
  }
  std::cout << "  after simplify() (" << simple.size() << " chars"
            << (simple.size() > raw.size() ? " - bigger, see below" : "")
            << "):\n"
            << indented(clipped(simple), "    ");
  if (simple.size() > raw.size())
    // Worth seeing at least once: "simplified" means normalised, not smaller.
    // XOR against a constant is a per-bit rewrite, so Z3 happily expands one
    // 64-bit bvxor into 64 negations of individual extracts. The solver reads
    // that form no worse; a human reads it much worse.
    std::cout << "  (simplify() normalises, it does not minimise)\n";
}

/// The executor's expression for one IGNIL operand - the bridge the two
/// listings are there to show: `t7:i64` in the IGNIL *is* this Z3 term.
/// `SymbolicExecutor::eval` does exactly this internally, but it is private;
/// the public readTemp/readRegister reproduce it for the two cases that carry
/// a computed value.
std::optional<z3::expr> operandExpr(const ignil::SymbolicExecutor &exec,
                                    const ignil::Value &v) {
  if (const auto *t = std::get_if<ignil::Temp>(&v))
    return exec.readTemp(*t);
  if (const auto *r = std::get_if<ignil::Reg>(&v))
    return exec.readRegister(*r);
  return std::nullopt; // a Const operand is already its own expression
}

/// Source `index` of the block's terminator, as the executor computed it:
/// the target of a JMP, or the condition of a CJMP.
std::optional<z3::expr> terminatorSrc(const ignil::Block &block,
                                      const ignil::SymbolicExecutor &exec,
                                      std::size_t index = 0) {
  if (block.empty())
    return std::nullopt;
  const ignil::Op &last = block.ops().back();
  if (index >= last.srcs.size())
    return std::nullopt;
  return operandExpr(exec, last.srcs[index]);
}

/// Disassemble, lift, and hand back the IGNIL block at `base`. The Graph owns
/// the block, so it is returned alongside it.
struct Lifted {
  dragon::Graph graph;
  const ignil::Block *block;
};

/// Create a Graph and lift the first basic block from the code
/// return the block and the Graph that owns it.
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
  showIGNIL(*lifted.block);

  ignil::SymbolicExecutor exec;
  exec.setRegister(x86::RAX, 0);
  const z3::expr a = exec.symbolizeRegister(x86::RDI, "a");
  const z3::expr b = exec.symbolizeRegister(x86::RSI, "b");

  const ignil::RunResult result = exec.run(*lifted.block);
  std::cout << "  " << lifted.block->size() << " IGNIL ops, "
            << result.stats.opsExecuted << " executed\n";

  const z3::expr rax = exec.readRegister(x86::RAX);

  // The point of the whole exercise. Two x86 instructions' worth of IGNIL
  // collapse into one term over the two symbols, and `simplify()` folds away
  // the `0 ^ a` that xoring against a zeroed RAX left behind.
  showExpr("RAX after the block", rax);

  // A flag is an expression like any other, and this is where most of those
  // IGNIL ops went. ZF is a comparison against zero reified as an 8-bit 0/1
  // value, because the register file is byte-granular - nothing downstream
  // reads it here, but the executor computed it anyway, exactly as the machine
  // would have.
  showExpr("ZF after the block", exec.readRegister(x86::ZF));

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
  showIGNIL(*lifted.block);

  ignil::SymbolicExecutor exec;
  exec.setRegister(x86::RDI, kBuffer);
  exec.setRegister(x86::RSI, kKey);
  exec.symbolizeMemory(kBuffer, 8, "flag");
  exec.run(*lifted.block);

  const z3::expr rax = exec.readRegister(x86::RAX);

  // What the LOAD produced: eight symbolic bytes concatenated little-endian,
  // so flag[7] - the highest address - is leftmost. Those eight symbols are
  // what Solution::bytes("flag") reads back out of the model below.
  showExpr("the 8 bytes at [RDI], as loaded", exec.readMemory(kBuffer, 64));

  // ...and the same term XORed with the key. Solving `this == expected` is
  // the whole crackme.
  showExpr("RAX after the block", rax);

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
    // Each successor carries the guard under which it is reached, already
    // simplified by stepBranch. A single-successor result means the other
    // guard came back unsat - the edge was pruned, not merely unlisted.
    std::cout << "    -> 0x" << std::hex << s.address << std::dec
              << "  (taken=" << (s.taken ? "true" : "false")
              << ")  under: " << oneLine(s.condition) << "\n";
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
    showIGNIL(*lifted.block);

    ignil::SymbolicExecutor exec;
    exec.symbolizeRegister(x86::RDI, "n");
    const ignil::RunResult r = exec.run(*lifted.block);

    // The CJMP's condition operand, as the executor computed it. `jl` is
    // SF != OF, and both flags trace back through the subtraction the `cmp`
    // lifted to - which is why a two-instruction compare-and-branch produces
    // a term this size before Z3's rewriter gets to it.
    if (auto cond = terminatorSrc(*lifted.block, exec))
      showExpr("the CJMP condition (jl = SF != OF)", *cond);
    printSuccessors(r);
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
    showIGNIL(*lifted.block);

    ignil::SymbolicExecutor exec;
    exec.mapMemory(kHandlers, table);
    exec.setRegister(x86::RDI, kBytecode);

    // A well-formed program only ever holds a valid opcode there.
    const z3::expr opcode = exec.symbolizeMemory(kBytecode, 1, "vm_opcode");
    exec.assume(z3::ult(opcode, exec.bv(handlers.size(), 8)));

    const ignil::RunResult r = exec.run(*lifted.block);

    // The fetched opcode, zero-extended into ESI by the movzx. This is the
    // value gap #4 used to lose: before the memory-source MOVZX forms were
    // lifted, this read UNDEF and the enumeration below found nothing.
    showExpr("the fetched VM opcode, in RSI", exec.readRegister(x86::RSI));

    // The JMP's target operand - a load from the handler table at a symbolic
    // index. `assume`ing the opcode in range is what makes this enumerable:
    // the successors below are exactly its satisfying values.
    if (auto target = terminatorSrc(*lifted.block, exec))
      showExpr("the computed jump target", *target);

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

    // Not the same term at all, and that is the lesson. Without the range
    // constraint the *load address* `rsi*8 + 0x6000` has 256 satisfying
    // values, over the concretisation budget - so the LOAD could not be
    // resolved to any table slot and returned a fresh unconstrained symbol
    // instead. The jump target is that bare symbol: no table, no handlers, and
    // nothing for enumeration to bite on. Both warnings below follow from it.
    if (auto target = terminatorSrc(*lifted.block, exec))
      showExpr("the jump target, unconstrained", *target);

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
    showIGNIL(*lifted.block);

    ignil::SymbolicExecutor exec;
    const z3::expr a = exec.symbolizeRegister(x86::RDI, "a");
    const z3::expr b = exec.symbolizeRegister(x86::RSI, "b");
    exec.run(*lifted.block);

    const z3::expr rax = exec.readRegister(x86::RAX);

    // The clearest illustration of what simplify() is not. The MBA survives it
    // intact - `(a|b) + (a&b)` is a rewriter's fixpoint, there is no local
    // rule that turns it into `a + b`. Only the equivalence *query* below
    // settles it, by failing to find a counterexample.
    showExpr("RAX after the MBA block", rax);

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

    showIGNIL(*lifted.block);

    ignil::SymbolicExecutor exec;
    exec.symbolizeRegister(x86::RDI, "x");
    exec.setRegister(x86::RSI, 0xA5A5);
    exec.run(*lifted.block);

    // x appears all over this term, and it still cannot affect the result.
    // evaluate() is what proves that: a candidate model, then a second query
    // asking whether any model disagrees.
    showExpr("RCX = ((x^k) + k) - (x^k)", exec.readRegister(x86::RCX));

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
