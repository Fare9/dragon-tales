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

#include "ToyVM.hpp"

#include <catch2/catch_test_macros.hpp>

#include <dragon/assembler/Assembler.hpp>
#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>
#include <dragon/lifters/llvm/LLVMLifter.hpp>

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>

#include <unicorn/unicorn.h>

#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>

using namespace dragon::test::toyvm;

namespace {

void requireUc(uc_err err, const char *what) {
  if (err != UC_ERR_OK)
    throw std::runtime_error(std::string(what) + ": " + uc_strerror(err));
}

std::vector<std::uint8_t> assembleInterpreter() {
  dragon::Configuration cfg;
  dragon::Assembler as(dragon::Architecture::X86_64, cfg);
  auto bytes = as.assemble(kInterpreterSource);
  REQUIRE_FALSE(bytes.empty());
  return bytes;
}

// Runs the interpreter under Unicorn and, if `trace` is non-null, records
// every address executed (in order, duplicates included - a real trace,
// not a set of unique addresses) via a UC_HOOK_CODE callback.
std::uint32_t runNative(const std::vector<std::uint8_t> &code,
                        const std::vector<std::uint8_t> &bytecode,
                        std::vector<std::uint64_t> *trace = nullptr) {
  uc_engine *uc = nullptr;
  requireUc(uc_open(UC_ARCH_X86, UC_MODE_64, &uc), "uc_open");

  requireUc(uc_mem_map(uc, kCodeBase, kPageSize, UC_PROT_ALL), "map code");
  requireUc(uc_mem_write(uc, kCodeBase, code.data(), code.size()), "write code");

  requireUc(uc_mem_map(uc, kBytecodeBase, kPageSize, UC_PROT_ALL), "map bytecode");
  requireUc(uc_mem_write(uc, kBytecodeBase, bytecode.data(), bytecode.size()),
           "write bytecode");

  requireUc(uc_mem_map(uc, kStackBase, kPageSize, UC_PROT_ALL), "map vstack");
  requireUc(uc_mem_map(uc, kNativeStackBase, kPageSize, UC_PROT_ALL), "map native stack");

  std::uint64_t rsi = kBytecodeBase, rdi = kStackBase;
  requireUc(uc_reg_write(uc, UC_X86_REG_RSI, &rsi), "write rsi");
  requireUc(uc_reg_write(uc, UC_X86_REG_RDI, &rdi), "write rdi");

  const std::uint64_t initialSP = kNativeStackBase + 0x800;
  std::uint64_t rsp = initialSP;
  requireUc(uc_reg_write(uc, UC_X86_REG_RSP, &rsp), "write rsp");
  requireUc(uc_mem_write(uc, initialSP, &kReturnSentinel, sizeof(kReturnSentinel)),
           "write ret sentinel");

  uc_hook hookHandle{};
  auto hookCb = +[](uc_engine *, std::uint64_t address, std::uint32_t, void *userData) {
    static_cast<std::vector<std::uint64_t> *>(userData)->push_back(address);
  };
  if (trace)
    requireUc(uc_hook_add(uc, &hookHandle, UC_HOOK_CODE, (void *) hookCb, trace,
                          kCodeBase, kCodeBase + kPageSize - 1),
             "hook code");

  requireUc(uc_emu_start(uc, kCodeBase, kReturnSentinel, 0, 0), "uc_emu_start");

  std::uint64_t eax = 0;
  requireUc(uc_reg_read(uc, UC_X86_REG_RAX, &eax), "read eax");

  uc_close(uc);
  return static_cast<std::uint32_t>(eax);
}

// Decodes the interpreter once, then replays `trace`'s addresses against
// that decode to build the ordered instruction sequence liftTrace() needs -
// exactly the pattern the `trace-capture` skill describes ("batch-decode
// the containing region once, then look addresses up").
dragon::ignil::Block liftCapturedTrace(const std::vector<std::uint8_t> &code,
                                       const std::vector<std::uint64_t> &trace) {
  dragon::Configuration cfg;
  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  auto decoded = dis.disassemble(code, kCodeBase);

  std::map<std::uint64_t, dragon::Instruction> byAddr;
  for (auto &insn: decoded)
    byAddr.emplace(insn.address(), insn);

  std::vector<dragon::Instruction> ordered;
  ordered.reserve(trace.size());
  for (auto addr: trace)
    ordered.push_back(byAddr.at(addr));

  return dis.liftTrace(ordered);
}

std::size_t countInstructions(llvm::Module &mod) {
  std::size_t n = 0;
  for (auto &fn: mod)
    for (auto it = llvm::inst_begin(fn), end = llvm::inst_end(fn); it != end; ++it)
      ++n;
  return n;
}

// JIT-executes `irText`'s `fnName` with rsi=kBytecodeBase, rdi=kStackBase,
// bytecode already written into the mem buffer - mirrors
// X86SemanticRoundTripFixture.h's runLifted(), parameterized so it can run
// both the pre- and post-optimization IR from the same LLVMLifter instance.
std::uint32_t jitRun(const std::string &irText, const std::string &fnName,
                     const std::vector<std::uint8_t> &bytecode) {
  static std::once_flag jitInitFlag;
  std::call_once(jitInitFlag, [] {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
  });

  auto ctx = std::make_unique<llvm::LLVMContext>();
  llvm::SMDiagnostic err;
  auto mod = llvm::parseIR(llvm::MemoryBufferRef(irText, "toy_vm"), err, *ctx);
  if (!mod)
    throw std::runtime_error("jitRun: parseIR failed: " + err.getMessage().str());

  auto jitOrErr = llvm::orc::LLJITBuilder().create();
  if (!jitOrErr)
    throw std::runtime_error("jitRun: LLJITBuilder failed");
  auto jit = std::move(*jitOrErr);

  llvm::orc::ThreadSafeContext tsCtx(std::move(ctx));
  llvm::orc::ThreadSafeModule tsm(std::move(mod), tsCtx);
  if (auto e = jit->addIRModule(std::move(tsm)))
    throw std::runtime_error("jitRun: addIRModule failed");

  auto symOrErr = jit->lookup(fnName);
  if (!symOrErr)
    throw std::runtime_error("jitRun: lookup(" + fnName + ") failed");
  auto fnPtr = symOrErr->toPtr<void (*)(std::uint8_t *, std::uint8_t *)>();

  // mem: flat buffer covering [0, kStackBase+page) - see
  // X86SemanticRoundTripFixture.h for why this "waste the low prefix"
  // approach is simpler than offsetting pointers to fake a base.
  std::vector<std::uint8_t> mem(kStackBase + kPageSize, 0);
  std::copy(bytecode.begin(), bytecode.end(), mem.begin() + kBytecodeBase);

  std::vector<std::uint8_t> regs(160, 0);
  auto setReg64 = [&](std::uint32_t offset, std::uint64_t value) {
    std::memcpy(regs.data() + offset, &value, sizeof(value));
  };
  setReg64(regoff::RSI, kBytecodeBase);
  setReg64(regoff::RDI, kStackBase);

  fnPtr(mem.data(), regs.data());

  std::uint64_t eax = 0;
  std::memcpy(&eax, regs.data() + regoff::RAX, sizeof(eax));
  return static_cast<std::uint32_t>(eax);
}

} // namespace

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
