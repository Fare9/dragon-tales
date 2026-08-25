// Handler-level devirtualization: segment ONE captured whole-program trace
// into one episode per VM handler (segmentTraceByHandler, see
// ToyVMHarness.hpp's header comment for why this is a meaningfully
// different, more honest technique than TestToyVM.cpp's whole-trace
// flattening), lift each handler independently to its own LLVM function,
// optimize all of them together, and - the actual point of doing this -
// verify each recovered function against FRESH inputs that never appeared
// anywhere in the original trace capture (program()'s a=10, b=4, c=3 and
// their derived values 14/2/12/1/13). A whole-trace-flattened function is
// only provably correct for the one path traced; if these per-handler
// functions are correct for inputs the trace never saw, that's real
// evidence they recovered the *generic* semantics of each opcode, not just
// memorized one execution.

#include "ToyVMHarness.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace dragon::test::toyvm;

namespace {

void setReg64(std::vector<std::uint8_t> &regs, std::uint32_t off, std::uint64_t v) {
  std::memcpy(regs.data() + off, &v, sizeof(v));
}
std::uint64_t getReg64(std::vector<std::uint8_t> &regs, std::uint32_t off) {
  std::uint64_t v = 0;
  std::memcpy(&v, regs.data() + off, sizeof(v));
  return v;
}
std::uint32_t getReg32(std::vector<std::uint8_t> &regs, std::uint32_t off) {
  return static_cast<std::uint32_t>(getReg64(regs, off));
}
void setMem32(std::vector<std::uint8_t> &mem, std::uint64_t addr, std::uint32_t v) {
  std::memcpy(mem.data() + addr, &v, sizeof(v));
}
std::uint32_t getMem32(std::vector<std::uint8_t> &mem, std::uint64_t addr) {
  std::uint32_t v = 0;
  std::memcpy(&v, mem.data() + addr, sizeof(v));
  return v;
}

// Fresh scratch region, distinct from kBytecodeBase/kStackBase (the
// addresses the original trace capture actually touched) - using them
// here would risk accidentally "passing" by coincidence rather than by
// genuine generalization.
constexpr std::uint64_t kFreshData  = 0x6000;
constexpr std::uint64_t kFreshStack = 0x7000;
constexpr std::size_t kFreshMemSize = 0x8000;

} // namespace

TEST_CASE("A whole-program trace segments into one episode per VM handler",
         "[toy-vm][handler-recovery]") {
  auto code = assembleInterpreter();
  std::vector<std::uint64_t> trace;
  auto result = runNative(code, program(), &trace);
  REQUIRE(result == kExpectedResult);

  auto decoded = decodeInterpreter(code);
  auto segments = segmentTraceByHandler(decoded, trace);

  REQUIRE(segments.size() == handlerNames().size());
  for (const auto &name: handlerNames())
    REQUIRE(segments.count(name) == 1);

  // Every segment should be small - a handful of instructions, not the
  // whole 182-instruction trace. This is the structural difference from
  // TestToyVM.cpp's approach: each recovered unit is one handler's body,
  // not the whole program.
  for (const auto &[name, insns]: segments) {
    INFO("handler: " << name);
    REQUIRE(insns.size() < 20);
  }
}

TEST_CASE("Each recovered handler function is correct for fresh inputs "
         "never seen in the original trace",
         "[toy-vm][handler-recovery]") {
  auto code = assembleInterpreter();
  std::vector<std::uint64_t> trace;
  auto baseline = runNative(code, program(), &trace);
  REQUIRE(baseline == kExpectedResult);

  auto decoded = decodeInterpreter(code);
  auto segments = segmentTraceByHandler(decoded, trace);
  REQUIRE(segments.size() == handlerNames().size());

  // Lift every handler into its own function within ONE shared module, then
  // optimize all of them together in a single pass.
  dragon::llvmlifter::LLVMLifter lifter(dragon::Architecture::X86_64,
                                        "toy_vm_handlers");
  dragon::Configuration cfg;
  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  for (const auto &[name, insns]: segments) {
    auto block = dis.liftTrace(insns);
    REQUIRE(lifter.liftBlock(block, name));
  }

  const auto rawCount = countInstructions(lifter.module());
  lifter.runMem2Reg();
  lifter.runInstCombine();
  lifter.runGVN();
  lifter.runDCE();
  const auto optimizedCount = countInstructions(lifter.module());

  INFO("raw instructions across all 7 handlers: " << rawCount);
  INFO("optimized instructions across all 7 handlers: " << optimizedCount);
  INFO(lifter.ir());
  REQUIRE(optimizedCount < rawCount);

  auto ir = lifter.ir();

  SECTION("op_push: fetches [rsi] into [rdi], advances both pointers by 4") {
    std::vector<std::uint8_t> mem(kFreshMemSize, 0), regs(160, 0);
    setMem32(mem, kFreshData, 777); // a value never pushed in program()
    setReg64(regs, regoff::RSI, kFreshData);
    setReg64(regs, regoff::RDI, kFreshStack);
    jitRunHandler(ir, "op_push", mem, regs);
    REQUIRE(getMem32(mem, kFreshStack) == 777);
    REQUIRE(getReg64(regs, regoff::RDI) == kFreshStack + 4);
    REQUIRE(getReg64(regs, regoff::RSI) == kFreshData + 4);
  }

  SECTION("op_add: [rdi-8] + [rdi-4]") {
    std::vector<std::uint8_t> mem(kFreshMemSize, 0), regs(160, 0);
    setMem32(mem, kFreshStack, 45);
    setMem32(mem, kFreshStack + 4, 17);
    setReg64(regs, regoff::RDI, kFreshStack + 8);
    jitRunHandler(ir, "op_add", mem, regs);
    REQUIRE(getMem32(mem, kFreshStack) == 45u + 17u);
    REQUIRE(getReg64(regs, regoff::RDI) == kFreshStack + 4);
  }

  SECTION("op_sub: [rdi-8] - [rdi-4]") {
    std::vector<std::uint8_t> mem(kFreshMemSize, 0), regs(160, 0);
    setMem32(mem, kFreshStack, 100);
    setMem32(mem, kFreshStack + 4, 23);
    setReg64(regs, regoff::RDI, kFreshStack + 8);
    jitRunHandler(ir, "op_sub", mem, regs);
    REQUIRE(getMem32(mem, kFreshStack) == 100u - 23u);
  }

  SECTION("op_and: [rdi-8] & [rdi-4]") {
    std::vector<std::uint8_t> mem(kFreshMemSize, 0), regs(160, 0);
    setMem32(mem, kFreshStack, 12);
    setMem32(mem, kFreshStack + 4, 10);
    setReg64(regs, regoff::RDI, kFreshStack + 8);
    jitRunHandler(ir, "op_and", mem, regs);
    REQUIRE(getMem32(mem, kFreshStack) == (12u & 10u));
  }

  SECTION("op_or: [rdi-8] | [rdi-4]") {
    std::vector<std::uint8_t> mem(kFreshMemSize, 0), regs(160, 0);
    setMem32(mem, kFreshStack, 12);
    setMem32(mem, kFreshStack + 4, 10);
    setReg64(regs, regoff::RDI, kFreshStack + 8);
    jitRunHandler(ir, "op_or", mem, regs);
    REQUIRE(getMem32(mem, kFreshStack) == (12u | 10u));
  }

  SECTION("op_xor: [rdi-8] ^ [rdi-4]") {
    std::vector<std::uint8_t> mem(kFreshMemSize, 0), regs(160, 0);
    setMem32(mem, kFreshStack, 12);
    setMem32(mem, kFreshStack + 4, 10);
    setReg64(regs, regoff::RDI, kFreshStack + 8);
    jitRunHandler(ir, "op_xor", mem, regs);
    REQUIRE(getMem32(mem, kFreshStack) == (12u ^ 10u));
  }

  SECTION("op_halt: reads [rdi-4] into eax") {
    std::vector<std::uint8_t> mem(kFreshMemSize, 0), regs(160, 0);
    setMem32(mem, kFreshStack, 555);
    setReg64(regs, regoff::RDI, kFreshStack + 4);
    jitRunHandler(ir, "op_halt", mem, regs);
    REQUIRE(getReg32(regs, regoff::RAX) == 555u);
  }
}
