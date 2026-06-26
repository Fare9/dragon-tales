#ifdef DRAGON_WITH_Z3

#include <dragon/analysis/BackwardSlicer.hpp>
#include <dragon/analysis/SliceExpr.hpp>
#include <dragon/analysis/SliceExprZ3.hpp>
#include <dragon/analysis/ValueEnumerator.hpp>
#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/x86/x86IGNILRegs.hpp>

#include <cstdint>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace x86 = dragon::x86;
using namespace dragon::slice;

static void printSeparator(char c = '-', int width = 72) {
  std::cout << std::string(width, c) << '\n';
}

static void printZ3Expr(const Expr &expr, z3::context &ctx) {
  SliceExprZ3 lowerer{ctx};
  std::cout << "  Z3 expr:    " << lowerer.lower(expr) << '\n';
}

// -----------------------------------------------------------------------
// Part 1 -- direct API: enumerate jump targets from a symbolic expression
//
// Simulates the situation where the backward slicer produced:
//
//   jmp rax   where rax = base + (index * 8),   index in {0, 1, 2}
//
// We build the SliceExpr by hand, add a Z3 constraint that pins index to
// those three values, and call enumeratedValues() to recover all targets.
// -----------------------------------------------------------------------

static void demoPart1() {
  printSeparator('=');
  std::cout << "  Part 1 -- direct API: enumerate targets from ADD(base, "
               "idx*8)\n";
  printSeparator('=');

  // Expression:  RAX + (RCX * 8)
  // This is what the slicer would return for:
  //   mov rax, 0x4000
  //   imul rcx, rcx, 8
  //   add rax, rcx
  //   jmp rax
  // (rax constant-folds; rcx stays symbolic)
  auto base = std::make_shared<Expr>(Const{0x4000ULL, 64});
  auto rcx = std::make_shared<Expr>(SymReg{x86::RCX});
  auto step = std::make_shared<Expr>(Const{8ULL, 64});
  auto offset =
          std::make_shared<Expr>(BinOp{dragon::ignil::Opcode::MUL, rcx, step});
  Expr jmpTarget{BinOp{dragon::ignil::Opcode::ADD, base, offset}};

  z3::context ctx;

  // SliceExprZ3 names RCX as "RCX:64".
  // Constrain: rcx in {0, 1, 2}  (3-entry jump table).
  z3::expr rcx_sym = ctx.bv_const("RCX:64", 64);
  z3::expr constraint =
          (rcx_sym == ctx.bv_val(static_cast<uint64_t>(0), 64u)) ||
          (rcx_sym == ctx.bv_val(static_cast<uint64_t>(1), 64u)) ||
          (rcx_sym == ctx.bv_val(static_cast<uint64_t>(2), 64u));

  std::cout << "  Expression: 0x4000 + (RCX * 8),  RCX in {0, 1, 2}\n";
  std::cout << "  SliceExpr:  " << toString(jmpTarget) << '\n';
  printZ3Expr(jmpTarget, ctx);
  std::cout << '\n';

  auto targets = enumeratedValues(jmpTarget, ctx, {constraint});

  std::cout << "  Enumerated jump targets (" << targets.size() << "):\n";
  for (std::uint64_t t: targets)
    std::cout << std::format("    0x{:x}\n", t);
  std::cout << '\n';
}

// -----------------------------------------------------------------------
// Part 2 -- end-to-end: disassemble a snippet with an indirect jump
//
//   0x2000:  mov rax, 0x3000     48 C7 C0 00 30 00 00
//   0x2007:  add rax, 8          48 83 C0 08
//   0x200B:  jmp rax             FF E0
//
// The backward slicer returns BinOp{ADD, Const{0x3000}, Const{8}}, not a
// plain Const, so the fast path in x86Analysis misses it.  With
// useZ3ForPathDiscovery=true, Z3 resolves 0x3000+8 = 0x3008.  The target
// is out of our byte range so it is logged as skipped -- but you can see
// the full Z3 resolution path in the output.
// -----------------------------------------------------------------------

static void demoPart2() {
  printSeparator('=');
  std::cout << "  Part 2 -- end-to-end disassembly with Z3 path discovery\n";
  printSeparator('=');

  // mov rax, 0x3000 ; add rax, 8 ; jmp rax
  const std::vector<std::uint8_t> bytes = {
          0x48, 0xC7, 0xC0, 0x00, 0x30, 0x00, 0x00, // mov rax, 0x3000
          0x48, 0x83, 0xC0, 0x08, // add rax, 8
          0xFF, 0xE0, // jmp rax
  };
  constexpr std::uint64_t base = 0x2000;

  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::RECURSIVE_TRAVERSAL;
  cfg.syntax = dragon::Syntax::INTEL;
  cfg.logging = true; // <-- see the Z3 resolution log
  cfg.useZ3ForPathDiscovery = true; // <-- engage Z3

  std::cout << "  Bytes @ 0x2000: mov rax,0x3000 / add rax,8 / jmp rax\n";
  std::cout << "  (logging=true -- diagnostic output follows)\n\n";

  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  dragon::Graph g(dragon::Architecture::X86_64, cfg);
  dis.disassemble(bytes, base, g);

  std::cout << "\n  Blocks found in graph:\n";
  for (const dragon::BasicBlock &b: g.blocks())
    std::cout << std::format("    0x{:x}\n", b.startAddress());
  std::cout << '\n';
}

// -----------------------------------------------------------------------
// Part 3 -- solveUnique: verify a single-target indirect call
//
// Builds the expression  RSP + 0x18  (a common ROP/return-address pattern)
// and uses solveUnique with a constraint that pins RSP to 0x7fff_ff00.
// -----------------------------------------------------------------------

static void demoPart3() {
  printSeparator('=');
  std::cout << "  Part 3 -- solveUnique: pin RSP and find the return address\n";
  printSeparator('=');

  auto rsp = std::make_shared<Expr>(SymReg{x86::RSP});
  auto disp = std::make_shared<Expr>(Const{0x18ULL, 64});
  Expr retAddr{BinOp{dragon::ignil::Opcode::ADD, rsp, disp}};

  z3::context ctx;
  z3::expr rsp_sym = ctx.bv_const("RSP:64", 64);
  z3::expr constraint =
          (rsp_sym == ctx.bv_val(static_cast<uint64_t>(0x7fffff00), 64u));

  std::cout << "  Expression: RSP + 0x18,  RSP == 0x7fffff00\n";
  std::cout << "  SliceExpr:  " << toString(retAddr) << '\n';
  printZ3Expr(retAddr, ctx);
  std::cout << '\n';

  auto result = solveUnique(retAddr, ctx, {constraint});
  if (result)
    std::cout << std::format("  solveUnique  ==>  0x{:x}\n", *result);
  else
    std::cout << "  solveUnique  ==>  (not uniquely determined)\n";
  std::cout << '\n';
}

// -----------------------------------------------------------------------

int main() {
  demoPart1();
  demoPart2();
  demoPart3();
  return 0;
}

#else // DRAGON_WITH_Z3

#include <iostream>
int main() {
  std::cout << "This example requires -DDRAGON_USE_Z3=ON at configure time.\n";
  return 1;
}

#endif // DRAGON_WITH_Z3
