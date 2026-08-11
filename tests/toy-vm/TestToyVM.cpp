// End-to-end validation of dragon-tales' planned VM-devirtualization
// pipeline, using the smallest possible real target: the toy VM in
// ToyVM.hpp. Four stages, each its own TEST_CASE so a failure pinpoints
// exactly which stage broke:
//
//   1. The hand-written interpreter actually computes the right answer
//      under real (Unicorn) emulation - a baseline sanity check on the
//      assembly itself, independent of dragon-tales entirely.
//   2. A real execution trace captured from that same run, fed through
//      Disassembler::liftTrace(), produces a large IGNIL block - proof the
//      dispatch loop really did get unrolled into one big obfuscated
//      straight-line program (as opposed to, say, silently capturing
//      nothing).
//   3. That IGNIL block, lifted to LLVM IR via the new
//      LLVMLifter::liftBlock() and JIT-executed with no optimization at
//      all, still computes the right answer - proof the *lift itself* is
//      faithful before any simplification is asked to do anything.
//   4. Running LLVMLifter's real optimize() pipeline (mem2reg -> instcombine
///     -> GVN -> DCE) over that IR shrinks it substantially *and* the
//      JIT-executed optimized function still computes the right answer -
//      proof the MBA/dispatch-loop noise actually collapses under ordinary
//      LLVM optimization, not just that the pipeline runs without crashing.
//
// This flattens ONE whole-program trace into ONE function - proof the
// mechanics work, but NOT the same technique as the back.engineering
// posts' guided symbolic execution (see ToyVMHarness.hpp's header comment
// for why). TestToyVMHandlerRecovery.cpp is the step closer: segmenting a
// trace into one episode per VM handler and lifting each independently.

#include "ToyVMHarness.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace dragon::test::toyvm;

// -----------------------------------------------------------------------
// Stage 1: the hand-written interpreter itself is correct.
// -----------------------------------------------------------------------

TEST_CASE("Toy VM interpreter computes the right answer under real emulation",
         "[toy-vm]") {
  auto code = assembleInterpreter();
  auto result = runNative(code, program());
  REQUIRE(result == kExpectedResult);
}

// -----------------------------------------------------------------------
// Stage 2: a captured trace lifts to a large, fully-unrolled IGNIL block.
// -----------------------------------------------------------------------

TEST_CASE("Captured trace lifts to a large obfuscated IGNIL block", "[toy-vm]") {
  auto code = assembleInterpreter();
  std::vector<std::uint64_t> trace;
  auto result = runNative(code, program(), &trace);
  REQUIRE(result == kExpectedResult);

  // 12 bytecode instructions, each paying for a multi-compare dispatch
  // pass plus a handler body - dozens of native x86 instructions, each of
  // which is itself several IGNIL ops (e.g. a single ADD needs its own
  // flag computation). This should be large.
  REQUIRE(trace.size() > 50);

  auto block = liftCapturedTrace(code, trace);
  REQUIRE(block.size() > 100);
}

// -----------------------------------------------------------------------
// Stage 3: the raw (unoptimized) lift is itself faithful.
// -----------------------------------------------------------------------

TEST_CASE("Raw (unoptimized) lifted trace JIT-executes to the right answer",
         "[toy-vm]") {
  auto code = assembleInterpreter();
  std::vector<std::uint64_t> trace;
  auto baseline = runNative(code, program(), &trace);
  REQUIRE(baseline == kExpectedResult);

  auto block = liftCapturedTrace(code, trace);

  dragon::llvmlifter::LLVMLifter lifter(dragon::Architecture::X86_64, "toy_vm");
  REQUIRE(lifter.liftBlock(block, "toy_vm_trace"));

  auto rawIR = lifter.ir();
  auto result = jitRun(rawIR, "toy_vm_trace", program());
  REQUIRE(result == kExpectedResult);
}

// -----------------------------------------------------------------------
// Stage 4: real LLVM optimization collapses the MBA/dispatch noise.
// -----------------------------------------------------------------------

TEST_CASE("LLVM optimization collapses the VM+MBA obfuscation and stays correct",
         "[toy-vm]") {
  auto code = assembleInterpreter();
  std::vector<std::uint64_t> trace;
  auto baseline = runNative(code, program(), &trace);
  REQUIRE(baseline == kExpectedResult);

  auto block = liftCapturedTrace(code, trace);

  dragon::llvmlifter::LLVMLifter lifter(dragon::Architecture::X86_64, "toy_vm");
  REQUIRE(lifter.liftBlock(block, "toy_vm_trace"));

  const std::size_t rawCount = countInstructions(lifter.module());

  // LLVMLifter's C++ API exposes the passes individually (Python's
  // convenience optimize() wrapper is a Python-only helper) - run the
  // recommended order by hand: mem2reg -> instcombine -> GVN -> DCE.
  lifter.runMem2Reg();
  lifter.runInstCombine();
  lifter.runGVN();
  lifter.runDCE();

  const std::size_t optimizedCount = countInstructions(lifter.module());
  auto optimizedIR = lifter.ir();

  INFO("raw IR instructions: " << rawCount);
  INFO("optimized IR instructions: " << optimizedCount);
  INFO(optimizedIR);

  // The real assertion: optimization must not have changed the answer.
  auto result = jitRun(optimizedIR, "toy_vm_trace", program());
  REQUIRE(result == kExpectedResult);

  // And it should have actually simplified something, not just survived -
  // this is the "did the obfuscation collapse" signal from the exercise.
  REQUIRE(optimizedCount < rawCount);
}
