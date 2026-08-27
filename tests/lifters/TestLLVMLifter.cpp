#include <catch2/catch_test_macros.hpp>

#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/llvm/LLVMLifter.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static const dragon::Function *findFunction(
        const std::vector<dragon::Function> &fns, std::uint64_t addr) {
  auto it = std::find_if(fns.begin(), fns.end(),
                         [addr](const dragon::Function &f) {
                           return f.startAddress() == addr;
                         });
  return it == fns.end() ? nullptr : &*it;
}

// -----------------------------------------------------------------------
// Regression: a discovered function start with no blocks
// -----------------------------------------------------------------------

TEST_CASE("LLVMLifter - lift() refuses a function with no blocks",
          "[llvm][lifter]") {
  // call 0x0     e8 fb ef ff ff   (rel32 = -0x1005, target outside the range)
  // ret          c3
  //
  // Byte sequence verified with:
  //   objdump -D -b binary -m i386:x86-64 -M intel --adjust-vma=0x1000
  //
  // Call targets are trusted as function starts unconditionally, so 0x0
  // becomes a discovered function with zero blocks - the ordinary shape of
  // a PLT stub reached from a raw .text blob, not a contrived input. lift()
  // used to segfault on exactly this: buildBlockMap()'s "missing IGNIL
  // block" guard only fires from inside its per-block loop, so a blockless
  // function passed it vacuously and then read llfn->getEntryBlock() on a
  // function with no basic blocks. lift() now rejects any invalid Function
  // view up front.
  const std::vector<std::uint8_t> bytes = {0xE8, 0xFB, 0xEF, 0xFF, 0xFF, 0xC3};

  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;

  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  dragon::Graph g(dragon::Architecture::X86_64, cfg);
  dis.disassemble(bytes, 0x1000, g);

  // 0x0 is a discovered function start (call targets are trusted
  // unconditionally) but has no blocks, so its Function view reports
  // itself invalid - exactly what the constructor documents.
  //
  // Built directly rather than via Graph::functions(), which filters
  // invalid views out. That filter is why a C++ caller iterating
  // functions() never saw this; the C shim's dragon_graph_get_function()
  // has no such filter, so C and Python callers reach it directly.
  const dragon::Function blockless(&g, 0x0);
  REQUIRE(g.isValidFunction(0x0));
  REQUIRE(blockless.blocks().empty());
  REQUIRE_FALSE(blockless.isValid());

  dragon::llvmlifter::LLVMLifter llvml{dragon::Architecture::X86_64,
                                       "blockless"};

  // Refused, not crashed - and nothing is left behind in the module.
  REQUIRE_FALSE(llvml.lift(blockless, g));
  REQUIRE(llvml.ir().find("sub_0") == std::string::npos);

  // The real function beside it still lifts, on the same lifter instance.
  const auto funcs = g.functions();
  const dragon::Function *real = findFunction(funcs, 0x1000);
  REQUIRE(real != nullptr);
  REQUIRE(real->isValid());
  REQUIRE(llvml.lift(*real, g));
}
