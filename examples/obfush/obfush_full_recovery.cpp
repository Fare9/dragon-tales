// Captures a REAL execution trace of `compute` calling into `VmDispatch`
// (real compiled bytes from obfush_repro.c's linked binary), lifts it to
// IGNIL and then LLVM IR, and shows LLVM's own optimizer (mem2reg +
// instcombine + GVN + DCE - no custom passes) recovering the actual `a + b`
// operation from the masked, compile-time-encrypted VM_ADD call.
//
// This only became possible after fixing 5 real X86IGNILLifter gaps
// (MOVSX64rr32, SAR32ri/SAR64ri, IMUL64rri32/rri8/32rri/rri8, LEA64_32r) -
// all hit by GCC's completely ordinary signed-division-by-constant lowering
// for `command / ~SALT_CMD`, not anything obfuscation-specific. Before that
// fix, `command` (the decrypted opcode driving the switch) was silently
// corrupted by UNDEF, and the recovered result was wrong. See
// ToDo/Roadmap.md for the full writeup, including the two dead ends hit
// first (a Graph-based multi-block lift() has no tail-call support yet;
// speculating the cause was PHI/poison turned out to be wrong).
//
// Trace capture mirrors tests/toy-vm/ToyVMHarness.hpp's runNative() - same
// technique, applied to a real compiled binary instead of a hand-written
// toy VM.

#include <dragon/analysis/BackwardSlicer.hpp>
#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/x86/x86IGNILRegs.hpp>
#include <dragon/lifters/llvm/LLVMLifter.hpp>

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>

#include <unicorn/unicorn.h>

#include <cstdint>
#include <cstring>
#include <format>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace {

void printHeader(const std::string &title) {
  std::cout << std::string(80, '=') << "\n  " << title << "\n"
            << std::string(80, '=') << "\n";
}

void requireUc(uc_err err, const char *what) {
  if (err != UC_ERR_OK)
    throw std::runtime_error(std::string(what) + ": " + uc_strerror(err));
}

// Real .text bytes of obfush_repro (VmDispatch + compute), extracted with
// `objcopy -O binary --only-section=.text obfush_repro obfush_repro.text.bin`
// from the binary built alongside this file - VmDispatch @ 0x1190,
// compute @ 0x1210 in the real linked layout, both mapped verbatim below at
// those same addresses so every relative jump/call already resolves
// correctly with no patching needed.
const std::vector<std::uint8_t> kTextBytes = {
        0xf3, 0x0f, 0x1e, 0xfa, 0x48, 0x83, 0xec, 0x08, 0xbe, 0x07, 0x00,
        0x00, 0x00, 0xbf, 0x0f, 0x00, 0x00, 0x00, 0xe8, 0x99, 0x01, 0x00,
        0x00, 0x48, 0x8d, 0x35, 0x8a, 0x0f, 0x00, 0x00, 0xbf, 0x02, 0x00,
        0x00, 0x00, 0x89, 0xc2, 0x31, 0xc0, 0xe8, 0xc4, 0xff, 0xff, 0xff,
        0x31, 0xc0, 0x48, 0x83, 0xc4, 0x08, 0xc3, 0x66, 0x2e, 0x0f, 0x1f,
        0x84, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x1f, 0x00, 0xf3, 0x0f,
        0x1e, 0xfa, 0x31, 0xed, 0x49, 0x89, 0xd1, 0x5e, 0x48, 0x89, 0xe2,
        0x48, 0x83, 0xe4, 0xf0, 0x50, 0x54, 0x45, 0x31, 0xc0, 0x31, 0xc9,
        0x48, 0x8d, 0x3d, 0xa1, 0xff, 0xff, 0xff, 0xff, 0x15, 0x13, 0x2f,
        0x00, 0x00, 0xf4, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x48, 0x8d, 0x3d, 0x39, 0x2f, 0x00, 0x00, 0x48, 0x8d,
        0x05, 0x32, 0x2f, 0x00, 0x00, 0x48, 0x39, 0xf8, 0x74, 0x15, 0x48,
        0x8b, 0x05, 0xf6, 0x2e, 0x00, 0x00, 0x48, 0x85, 0xc0, 0x74, 0x09,
        0xff, 0xe0, 0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00, 0xc3, 0x0f,
        0x1f, 0x80, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8d, 0x3d, 0x09, 0x2f,
        0x00, 0x00, 0x48, 0x8d, 0x35, 0x02, 0x2f, 0x00, 0x00, 0x48, 0x29,
        0xfe, 0x48, 0x89, 0xf0, 0x48, 0xc1, 0xee, 0x3f, 0x48, 0xc1, 0xf8,
        0x03, 0x48, 0x01, 0xc6, 0x48, 0xd1, 0xfe, 0x74, 0x14, 0x48, 0x8b,
        0x05, 0xc5, 0x2e, 0x00, 0x00, 0x48, 0x85, 0xc0, 0x74, 0x08, 0xff,
        0xe0, 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00, 0xc3, 0x0f, 0x1f, 0x80,
        0x00, 0x00, 0x00, 0x00, 0xf3, 0x0f, 0x1e, 0xfa, 0x80, 0x3d, 0xc5,
        0x2e, 0x00, 0x00, 0x00, 0x75, 0x2b, 0x55, 0x48, 0x83, 0x3d, 0xa2,
        0x2e, 0x00, 0x00, 0x00, 0x48, 0x89, 0xe5, 0x74, 0x0c, 0x48, 0x8b,
        0x3d, 0xa6, 0x2e, 0x00, 0x00, 0xe8, 0xd9, 0xfe, 0xff, 0xff, 0xe8,
        0x64, 0xff, 0xff, 0xff, 0xc6, 0x05, 0x9d, 0x2e, 0x00, 0x00, 0x01,
        0x5d, 0xc3, 0x0f, 0x1f, 0x00, 0xc3, 0x0f, 0x1f, 0x80, 0x00, 0x00,
        0x00, 0x00, 0xf3, 0x0f, 0x1e, 0xfa, 0xe9, 0x77, 0xff, 0xff, 0xff,
        0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00, 0xf3, 0x0f, 0x1e, 0xfa,
        0x48, 0x63, 0xc7, 0xc1, 0xff, 0x1f, 0x89, 0xf1, 0x89, 0xd6, 0x48,
        0x69, 0xc0, 0x41, 0xdd, 0xb3, 0x20, 0xba, 0x17, 0x00, 0x00, 0x00,
        0x29, 0xca, 0xb9, 0x1f, 0x00, 0x00, 0x00, 0x29, 0xf1, 0x48, 0xc1,
        0xf8, 0x26, 0x29, 0xc7, 0x83, 0xc7, 0x01, 0x81, 0xff, 0xc2, 0x38,
        0x00, 0x00, 0x74, 0x34, 0x7f, 0x1a, 0x8d, 0x04, 0x0a, 0x81, 0xff,
        0xb2, 0x11, 0x00, 0x00, 0x74, 0x23, 0x29, 0xca, 0x31, 0xc0, 0x81,
        0xff, 0x3a, 0x25, 0x00, 0x00, 0x0f, 0x44, 0xc2, 0xc3, 0x90, 0x31,
        0xc0, 0x81, 0xff, 0x4a, 0x4c, 0x00, 0x00, 0x75, 0x16, 0x83, 0xfe,
        0x1f, 0x74, 0x05, 0x89, 0xd0, 0x99, 0xf7, 0xf9, 0xc3, 0x0f, 0x1f,
        0x00, 0x89, 0xd0, 0x0f, 0xaf, 0xc1, 0xc3, 0x66, 0x90, 0xc3, 0x0f,
        0x1f, 0x40, 0x00, 0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00,
        0x00, 0x00, 0x00, 0xf3, 0x0f, 0x1e, 0xfa, 0xba, 0x1f, 0x00, 0x00,
        0x00, 0x29, 0xf2, 0xbe, 0x17, 0x00, 0x00, 0x00, 0x29, 0xfe, 0xbf,
        0x9b, 0x60, 0xdd, 0xff, 0xe9, 0x64, 0xff, 0xff, 0xff,
};

constexpr std::uint64_t kTextBase = 0x1060;
constexpr std::uint64_t kComputeAddr = 0x1210;
constexpr std::uint64_t kStackBase = 0x9000000;
constexpr std::uint64_t kPageSize = 0x1000;
constexpr std::uint64_t kReturnSentinel = 0xdeadbeef000;

// Runs compute(a, b) under Unicorn, capturing the executed address trace.
struct RunResult {
  std::int32_t result;
  std::vector<std::uint64_t> trace;
};

RunResult runNative(std::int32_t a, std::int32_t b) {
  uc_engine *uc = nullptr;
  requireUc(uc_open(UC_ARCH_X86, UC_MODE_64, &uc), "uc_open");

  requireUc(uc_mem_map(uc, 0x1000, kPageSize, UC_PROT_ALL), "map text page");
  requireUc(uc_mem_write(uc, kTextBase, kTextBytes.data(), kTextBytes.size()),
           "write text");

  requireUc(uc_mem_map(uc, kStackBase, kPageSize, UC_PROT_ALL), "map stack");
  std::uint64_t initialSP = kStackBase + 0x800;
  requireUc(uc_mem_write(uc, initialSP, &kReturnSentinel,
                         sizeof(kReturnSentinel)),
           "write ret sentinel");
  requireUc(uc_reg_write(uc, UC_X86_REG_RSP, &initialSP), "write rsp");

  std::uint64_t edi = static_cast<std::uint32_t>(a);
  std::uint64_t esi = static_cast<std::uint32_t>(b);
  requireUc(uc_reg_write(uc, UC_X86_REG_RDI, &edi), "write edi");
  requireUc(uc_reg_write(uc, UC_X86_REG_RSI, &esi), "write esi");

  RunResult rr;
  uc_hook hookHandle{};
  auto hookCb = +[](uc_engine *, std::uint64_t address, std::uint32_t,
                    void *userData) {
    static_cast<std::vector<std::uint64_t> *>(userData)->push_back(address);
  };
  requireUc(uc_hook_add(uc, &hookHandle, UC_HOOK_CODE, (void *) hookCb,
                       &rr.trace, 0x1000, 0x1000 + kPageSize - 1),
           "hook code");

  requireUc(uc_emu_start(uc, kComputeAddr, kReturnSentinel, 0, 0),
           "uc_emu_start");

  std::uint64_t eax = 0;
  requireUc(uc_reg_read(uc, UC_X86_REG_RAX, &eax), "read eax");
  rr.result = static_cast<std::int32_t>(eax);
  uc_close(uc);
  return rr;
}

// JIT-executes `fnName(mem, regs)` with EDI=a, ESI=b already set, and
// returns whatever ends up in EAX - mirrors ToyVMHarness.hpp's jitRun().
std::int32_t jitRun(const std::string &irText, const std::string &fnName,
                    std::int32_t a, std::int32_t b) {
  static std::once_flag jitInitFlag;
  std::call_once(jitInitFlag, [] {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
  });

  auto ctx = std::make_unique<llvm::LLVMContext>();
  llvm::SMDiagnostic err;
  auto mod = llvm::parseIR(llvm::MemoryBufferRef(irText, "obfush"), err, *ctx);
  if (!mod)
    throw std::runtime_error("jitRun: parseIR failed: " +
                             err.getMessage().str());

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

  std::vector<std::uint8_t> mem(kPageSize, 0);
  std::vector<std::uint8_t> regs(160, 0);
  auto setReg32 = [&](std::uint32_t offset, std::int32_t value) {
    std::memcpy(regs.data() + offset, &value, sizeof(value));
  };
  setReg32(dragon::x86::kOffRDI, a);
  setReg32(dragon::x86::kOffRSI, b);

  fnPtr(mem.data(), regs.data());

  std::int32_t eax = 0;
  std::memcpy(&eax, regs.data() + dragon::x86::kOffRAX, sizeof(eax));
  return eax;
}

} // namespace

int main() {
  printHeader("obfush_repro: full trace -> IGNIL -> LLVM IR recovery");
  std::cout
          << "Captures a real Unicorn execution trace of compute(a,b) calling\n"
             "into VmDispatch (real compiled bytes), lifts it straight-line\n"
             "(Disassembler::liftTrace -> LLVMLifter::liftBlock - the same\n"
             "technique tests/toy-vm/ uses, no custom passes), and checks\n"
             "whether LLVM's ordinary optimizer recovers the real operation.\n\n";

  RunResult traced = runNative(15, 7);
  std::cout << std::format(
          "Native trace: compute(15, 7) = {} (expect 22), {} instructions "
          "executed.\n\n",
          traced.result, traced.trace.size());

  dragon::Configuration cfg;
  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  std::vector<dragon::Instruction> decoded = dis.disassemble(kTextBytes, kTextBase);

  std::map<std::uint64_t, dragon::Instruction> byAddr;
  for (auto &insn: decoded)
    byAddr.emplace(insn.address(), insn);

  std::vector<dragon::Instruction> ordered;
  for (auto a: traced.trace)
    ordered.push_back(byAddr.at(a));

  dragon::ignil::Block block = dis.liftTrace(ordered);

  printHeader("Lifted + optimized LLVM IR");
  dragon::llvmlifter::LLVMLifter lifter(dragon::Architecture::X86_64,
                                        "obfush_traced");
  if (!lifter.liftBlock(block, "compute_traced")) {
    std::cerr << "liftBlock failed\n";
    return 1;
  }

  std::cout << "LLVM IR Before Optimization:\n";

  std::cout << lifter.ir() << "\n";

  std::cout << std::format("(raw IR: {} chars)\n\n", lifter.ir().size());

  const auto rawCount = lifter.ir().size();
  lifter.runMem2Reg();
  lifter.runInstCombine();
  lifter.runGVN();
  lifter.runDCE();
  
  std::cout << "LLVM IR After Optimization:\n";

  std::string ir = lifter.ir();
  std::cout << ir << "\n";
  std::cout << std::format("(raw IR: {} chars -> optimized: {} chars)\n\n",
                           rawCount, ir.size());

  bool foundAdd = ir.find("add i32") != std::string::npos;
  std::cout << std::format(
          "Contains a plain `add i32` (the real recovered operation, not "
          "the\nmasked/encrypted call-site arithmetic)? {}\n\n",
          foundAdd ? "YES" : "NO");

  printHeader("JIT verification - including a FRESH input never traced");
  for (auto [a, b]: {std::pair{15, 7}, std::pair{100, 23}, std::pair{-4, 9}}) {
    std::int32_t got = jitRun(ir, "compute_traced", a, b);
    std::int32_t want = a + b;
    std::cout << std::format("  compute_traced({}, {}) = {} (expect {}) - {}\n",
                             a, b, got, want, got == want ? "OK" : "MISMATCH");
  }

  return 0;
}
