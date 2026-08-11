// Runs the MBA-obfuscated toy VM from tests/toy-vm/ and prints every stage
// of the whole-trace-flattening pipeline: the captured execution trace,
// the obfuscated IGNIL lift, and the recovered (optimized) LLVM IR - see
// tests/toy-vm/ToyVM.hpp for the full design rationale and the real
// X86IGNILLifter/LLVMLifter gaps this exercise found, and
// tests/toy-vm/ToyVMHarness.hpp for why this ("flatten one whole-program
// trace into one function") is a meaningfully different, simpler technique
// than the guided symbolic execution the back.engineering posts describe -
// toy_vm_handler_recovery.cpp is the step closer to that.
//
// This is the "let me actually see it" companion to
// tests/toy-vm/TestToyVM.cpp, which only asserts correctness (via
// Catch2 REQUIRE, output suppressed unless run with -s or a test fails).

#include "../../tests/toy-vm/ToyVMHarness.hpp"

#include <format>
#include <iostream>

using namespace dragon::test::toyvm;

namespace {

void printHeader(const std::string &title) {
  std::cout << "\n" << std::string(80, '=') << "\n  " << title << "\n"
            << std::string(80, '=') << "\n";
}

} // namespace

int main() {
  printHeader("Toy VM Devirtualization Demo (whole-trace flattening)");
  std::cout << "Bytecode program computes ((a+b) ^ (a&c)) | (b-c) for a=10, "
               "b=4, c=3\nExpected result: "
            << kExpectedResult
            << "\nEvery arithmetic handler is obfuscated via a classic MBA "
               "identity - see\ntests/toy-vm/ToyVM.hpp for the exact "
               "identities and the real lifter gaps found\nwhile building "
               "this.\n";

  auto code = assembleInterpreter();
  auto bytecode = program();

  // -- Stage 1: native ground truth + trace capture --------------------------
  printHeader("Stage 1: Native execution (Unicorn) + trace capture");
  std::vector<std::uint64_t> traceAddrs;
  auto nativeResult = runNative(code, bytecode, &traceAddrs);
  std::cout << std::format("Native result: {} (expected {}) - {}\n", nativeResult,
                           kExpectedResult,
                           nativeResult == kExpectedResult ? "OK" : "MISMATCH");
  std::cout << std::format("Captured trace: {} native x86-64 instructions "
                           "(dispatch loop unrolled, duplicates included)\n\n",
                           traceAddrs.size());

  auto decoded = decodeInterpreter(code);
  auto ordered = instructionsForTrace(decoded, traceAddrs);
  for (auto &insn: ordered)
    std::cout << std::format("  0x{:04x}  {}\n", insn.address(),
                             std::string(insn.disassembly()));

  // -- Stage 2: lift the trace to IGNIL ---------------------------------------
  printHeader("Stage 2: Disassembler::liftTrace() -> obfuscated IGNIL");
  auto block = liftCapturedTrace(code, traceAddrs);
  std::cout << std::format(
          "IGNIL block: {} ops (fully unrolled - every dispatch pass and "
          "MBA-identity\nhandler body is flattened into this one straight-line "
          "sequence). First 25 ops:\n\n",
          block.size());
  {
    std::size_t shown = 0;
    for (const auto &op: block) {
      if (shown++ >= 25) {
        std::cout << std::format("  ... ({} more ops)\n", block.size() - 25);
        break;
      }
      std::cout << "  " << dragon::ignil::toString(op) << "\n";
    }
  }

  // -- Stage 3: lift to LLVM IR + JIT (raw) -----------------------------------
  printHeader("Stage 3: LLVMLifter::liftBlock() -> raw LLVM IR (unoptimized)");
  dragon::llvmlifter::LLVMLifter lifter(dragon::Architecture::X86_64, "toy_vm");
  if (!lifter.liftBlock(block, "toy_vm_trace")) {
    std::cerr << "liftBlock failed\n";
    return 1;
  }
  auto rawIR = lifter.ir();
  auto rawCount = countInstructions(lifter.module());
  auto rawResult = jitRun(rawIR, "toy_vm_trace", bytecode);
  std::cout << std::format(
          "Raw IR: {} LLVM instructions. JIT-executed result: {} (expected {}) "
          "- {}\n",
          rawCount, rawResult, kExpectedResult,
          rawResult == kExpectedResult ? "OK" : "MISMATCH");

  // -- Stage 4: optimize + JIT (recovered code) -------------------------------
  printHeader("Stage 4: LLVM optimization -> recovered code");
  lifter.runMem2Reg();
  lifter.runInstCombine();
  lifter.runGVN();
  lifter.runDCE();
  auto optimizedIR = lifter.ir();
  auto optimizedCount = countInstructions(lifter.module());
  auto optimizedResult = jitRun(optimizedIR, "toy_vm_trace", bytecode);
  std::cout << std::format(
          "Optimized IR: {} LLVM instructions ({}x smaller than raw). "
          "JIT-executed\nresult: {} (expected {}) - {}\n\n",
          optimizedCount, rawCount / std::max<std::size_t>(optimizedCount, 1),
          optimizedResult, kExpectedResult,
          optimizedResult == kExpectedResult ? "OK" : "MISMATCH");
  std::cout << "--- Recovered code (full optimized LLVM IR) ---\n"
            << optimizedIR << "\n";

  return 0;
}
