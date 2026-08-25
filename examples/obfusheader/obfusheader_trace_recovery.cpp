// Captures a REAL Unicorn execution trace of `sum_to_n(n)` from the real,
// unmodified obfusheader_repro binary (examples/obfusheader/), lifts it to
// IGNIL and then LLVM IR, and shows what LLVM's own optimizer (mem2reg +
// instcombine + GVN + DCE, no custom passes) does with it.
//
// This is the exact function discussed manually in Binary Ninja: `for (int
// i = 1; i <= n; i++)` compiles, under CFLOW_BRANCHING, to a loop condition
// wrapped in `int_proxy()`'s opaque predicate. The real bytes at one such
// check site are:
//
//     xor rax, rax          ; genuinely always zero - not disguised at all
//     je  +2                ; unconditionally taken (ZF=1 from the xor)
//   ; fall-through (NEVER taken - decoded here for a linear-sweep reader):
//     add [rsi+0xf], ah
//     out dx, eax           ; privileged - would fault if ever really run
//     shl [rsi+0xf], 0xef
//     shl [rsi+0xf], cl
//     out dx, eax
//     leave
//   ; je's real target (2 bytes INTO the fall-through's own byte range):
//     pxor xmm0, xmm0
//     pxor xmm2, xmm2
//     pxor xmm1, xmm1
//
// The fall-through and the branch target are the *same bytes*, decoded
// from two different starting offsets - classic overlapping-instruction
// anti-disassembly, stacked on top of the opaque predicate itself. A
// disassembler has to resolve both tricks (know the predicate is constant,
// know the real target overlaps the decoy) to see through this statically.
//
// Capturing a real trace sidesteps both tricks *by construction*, with no
// dragon-tales logic aimed at either one specifically: Unicorn only ever
// executes the addresses a real run actually takes, so the fall-through
// junk (OUT/SHL/LEAVE) is never even decoded, let alone lifted.
//
// IMPORTANT DIFFERENCE from examples/obfush/obfush_full_recovery.cpp's
// technique: that target had no overlapping-instruction trick, so a single
// upfront linear sweep of the whole blob was safe (every traced address
// was also where the sweep happened to start an instruction). That is NOT
// safe here - a linear sweep starting at the fall-through's first byte
// would never independently re-decode starting at the branch target 2
// bytes later, so it would have no entry for that address at all. Instead,
// this file re-decodes fresh at *every* traced address via
// Disassembler::disassembleBlock(...)[0], exactly mirroring how a real
// disassembler (Binary Ninja included) has to decode from wherever control
// flow actually starts, not wherever a prior sweep happened to land.
//
// Scope note: `sum_to_n` has a real loop, unlike obfush_repro's straight-
// line `compute`. A captured trace is that one concrete run, unrolled flat
// (Disassembler::liftTrace has no notion of "the same block visited N
// times" - it is N copies of that block's ops in sequence). This is NOT
// general loop recovery (no induction-variable/back-edge structure is
// reconstructed) - it demonstrates that the opaque-predicate/overlapping-
// instruction scaffolding disappears for a real, traced execution, not
// that dragon-tales turns the obfuscated binary back into a generic,
// parametrized `for` loop. Two different `n` values are each traced and
// lifted independently below to show the result isn't a fluke tied to one
// specific input, not to claim a single recovered function that handles
// both.

#include <dragon/disassembler/Disassembler.hpp>
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

// sum_to_n's own code, extracted with
// `objcopy -O binary --only-section=.text obfusheader_repro /tmp/text.bin`
// then sliced to just its 0x2050-0x2269 byte range (real vaddrs in the
// linked binary) - confirmed by hand (objdump -d) to make zero `call`s and
// touch no memory outside the .data range embedded below, so it is fully
// self-contained: no libc, no other function, needed to run it in Unicorn.
constexpr std::uint64_t kFnAddr = 0x2050;
const std::vector<std::uint8_t> kSumToNBytes = {
        0xf3, 0x0f, 0x1e, 0xfa, 0x53, 0x41, 0x89, 0xf8, 0x31, 0xc9, 0x31, 0xff, 0x41,
        0x8d, 0x70, 0x01, 0x0f, 0xb6, 0x15, 0xa9, 0x3f, 0x00, 0x00, 0x44, 0x0f, 0xbe,
        0x0d, 0xa2, 0x3f, 0x00, 0x00, 0x0f, 0xbe, 0x1d, 0xa2, 0x3f, 0x00, 0x00, 0x48,
        0x31, 0xc0, 0x74, 0x01, 0x00, 0x66, 0x0f, 0xef, 0xc0, 0x66, 0x0f, 0xef, 0xd2,
        0x66, 0x0f, 0xef, 0xc9, 0x0f, 0xbe, 0xc2, 0x83, 0xc0, 0x07, 0x31, 0xd2, 0x44,
        0x0f, 0xbe, 0x1d, 0x7a, 0x3f, 0x00, 0x00, 0x44, 0x0f, 0xbe, 0x15, 0x76, 0x3f,
        0x00, 0x00, 0x41, 0xf7, 0xf1, 0xf2, 0x41, 0x0f, 0x2a, 0xc2, 0xf2, 0x41, 0x0f,
        0x2a, 0xd3, 0xf2, 0x0f, 0x58, 0xc0, 0x31, 0xd2, 0xf2, 0x0f, 0x5c, 0xd0, 0x29,
        0xd8, 0xf2, 0x48, 0x0f, 0x2a, 0xc8, 0xf2, 0x0f, 0x59, 0xca, 0xf2, 0x0f, 0x11,
        0x4c, 0x24, 0xf0, 0x0f, 0xbe, 0x05, 0x41, 0x3f, 0x00, 0x00, 0x44, 0x0f, 0xbe,
        0x0d, 0x3a, 0x3f, 0x00, 0x00, 0x44, 0x0f, 0xbe, 0x15, 0x39, 0x3f, 0x00, 0x00,
        0x83, 0xc0, 0x07, 0x41, 0xf7, 0xf1, 0x44, 0x39, 0xd0, 0x74, 0x1f, 0x0f, 0xbe,
        0x15, 0x25, 0x3f, 0x00, 0x00, 0x0f, 0xbe, 0x05, 0x51, 0x3f, 0x00, 0x00, 0x8d,
        0x04, 0xc0, 0x8d, 0x54, 0x02, 0x06, 0xb8, 0x02, 0x00, 0x00, 0x00, 0x83, 0xfa,
        0x09, 0x75, 0x4e, 0x0f, 0xb6, 0x05, 0x00, 0x3f, 0x00, 0x00, 0x0f, 0xb6, 0x05,
        0xfa, 0x3e, 0x00, 0x00, 0x31, 0xd2, 0x66, 0x0f, 0xef, 0xc0, 0x0f, 0xb6, 0x05,
        0xf4, 0x3e, 0x00, 0x00, 0x0f, 0xbe, 0x05, 0xe5, 0x3e, 0x00, 0x00, 0x44, 0x0f,
        0xbe, 0x0d, 0xde, 0x3e, 0x00, 0x00, 0x44, 0x0f, 0xbe, 0x15, 0xdd, 0x3e, 0x00,
        0x00, 0x83, 0xc0, 0x07, 0xf2, 0x0f, 0x10, 0x4c, 0x24, 0xf0, 0x41, 0xf7, 0xf1,
        0x44, 0x29, 0xd0, 0xf2, 0x48, 0x0f, 0x2a, 0xc0, 0xf2, 0x0f, 0x59, 0xc1, 0xf2,
        0x0f, 0x2c, 0xc0, 0x39, 0xc7, 0x7d, 0x3d, 0xb8, 0x01, 0x00, 0x00, 0x00, 0x45,
        0x85, 0xc0, 0x7e, 0x26, 0x40, 0xf6, 0xc6, 0x01, 0x75, 0x15, 0xb8, 0x02, 0x00,
        0x00, 0x00, 0x83, 0xc1, 0x01, 0x39, 0xf0, 0x74, 0x14, 0x66, 0x0f, 0x1f, 0x84,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x8d, 0x4c, 0x41, 0x01, 0x83, 0xc0, 0x02, 0x39,
        0xf0, 0x75, 0xf5, 0x83, 0xc7, 0x01, 0xe9, 0xcd, 0xfe, 0xff, 0xff, 0x0f, 0x1f,
        0x44, 0x00, 0x00, 0x0f, 0xb6, 0x15, 0x71, 0x3e, 0x00, 0x00, 0x0f, 0xbe, 0x35,
        0x6b, 0x3e, 0x00, 0x00, 0x44, 0x0f, 0xbe, 0x0d, 0x6a, 0x3e, 0x00, 0x00, 0x48,
        0x31, 0xc0, 0x74, 0x01, 0x00, 0x66, 0x0f, 0xef, 0xc0, 0x66, 0x0f, 0xef, 0xd2,
        0x66, 0x0f, 0xef, 0xc9, 0x0f, 0xbe, 0xc2, 0x83, 0xc0, 0x07, 0x31, 0xd2, 0x44,
        0x0f, 0xbe, 0x05, 0x42, 0x3e, 0x00, 0x00, 0x0f, 0xbe, 0x3d, 0x3f, 0x3e, 0x00,
        0x00, 0xf7, 0xf6, 0xf2, 0x0f, 0x2a, 0xc7, 0xf2, 0x41, 0x0f, 0x2a, 0xd0, 0xf2,
        0x0f, 0x58, 0xc0, 0x31, 0xd2, 0xf2, 0x0f, 0x5c, 0xd0, 0x44, 0x29, 0xc8, 0xf2,
        0x48, 0x0f, 0x2a, 0xc8, 0xf2, 0x0f, 0x59, 0xca, 0xf2, 0x0f, 0x11, 0x4c, 0x24,
        0xf8, 0x0f, 0xbe, 0x05, 0x0b, 0x3e, 0x00, 0x00, 0x0f, 0xbe, 0x35, 0x05, 0x3e,
        0x00, 0x00, 0x0f, 0xbe, 0x3d, 0x05, 0x3e, 0x00, 0x00, 0x83, 0xc0, 0x07, 0xf7,
        0xf6, 0x39, 0xf8, 0x74, 0x1a, 0x0f, 0xbe, 0x15, 0xf3, 0x3d, 0x00, 0x00, 0x0f,
        0xbe, 0x05, 0x1f, 0x3e, 0x00, 0x00, 0x8d, 0x04, 0xc0, 0x8d, 0x44, 0x02, 0x06,
        0x83, 0xf8, 0x09, 0x75, 0x30, 0x0f, 0xb6, 0x05, 0xd3, 0x3d, 0x00, 0x00, 0x0f,
        0xb6, 0x05, 0xcd, 0x3d, 0x00, 0x00, 0x0f, 0xb6, 0x05, 0xcd, 0x3d, 0x00, 0x00,
        0x0f, 0xb6, 0x05, 0xbe, 0x3d, 0x00, 0x00, 0x0f, 0xb6, 0x05, 0xb8, 0x3d, 0x00,
        0x00, 0x0f, 0xb6, 0x05, 0xb8, 0x3d, 0x00, 0x00, 0xf2, 0x0f, 0x10, 0x44, 0x24,
        0xf8, 0x89, 0xc8, 0x5b, 0xc3,
};

// .data - the compile-time-"encrypted" globals CONST_ENCRYPTION reads
// (bytes 9,8,7,...,1 at 0x6010-0x6018, plus a couple of unrelated
// neighbours at the same section) - extracted the same way, `--only-
// section=.data`. sum_to_n never writes memory outside its own stack
// frame, so this blob only ever needs to be *read*.
constexpr std::uint64_t kDataAddr = 0x6000;
const std::vector<std::uint8_t> kDataBytes = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x60, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x50,
        0x44, 0x49, 0x41, 0x4c, 0x53, 0x7a, 0x79, 0x78, 0x77, 0x76, 0x75, 0x74, 0x73,
        0x72, 0x71, 0x70, 0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x69, 0x68, 0x67, 0x66,
        0x65, 0x64, 0x63, 0x62, 0x61, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50,
        0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr std::uint64_t kCodePage = 0x2000;
constexpr std::uint64_t kDataPage = 0x6000;
constexpr std::uint64_t kStackBase = 0x9000000;
constexpr std::uint64_t kPageSize = 0x1000;
constexpr std::uint64_t kReturnSentinel = 0xdeadbeef000;

struct RunResult {
  std::int32_t result;
  std::vector<std::uint64_t> trace;
};

// Runs sum_to_n(n) under Unicorn, capturing the executed address trace.
RunResult runNative(std::int32_t n) {
  uc_engine *uc = nullptr;
  requireUc(uc_open(UC_ARCH_X86, UC_MODE_64, &uc), "uc_open");

  requireUc(uc_mem_map(uc, kCodePage, kPageSize, UC_PROT_ALL), "map code");
  requireUc(uc_mem_write(uc, kFnAddr, kSumToNBytes.data(), kSumToNBytes.size()),
           "write code");
  requireUc(uc_mem_map(uc, kDataPage, kPageSize, UC_PROT_ALL), "map data");
  requireUc(uc_mem_write(uc, kDataAddr, kDataBytes.data(), kDataBytes.size()),
           "write data");

  requireUc(uc_mem_map(uc, kStackBase, kPageSize, UC_PROT_ALL), "map stack");
  std::uint64_t initialSP = kStackBase + 0x800;
  requireUc(uc_mem_write(uc, initialSP, &kReturnSentinel,
                         sizeof(kReturnSentinel)),
           "write ret sentinel");
  requireUc(uc_reg_write(uc, UC_X86_REG_RSP, &initialSP), "write rsp");

  std::uint64_t edi = static_cast<std::uint32_t>(n);
  requireUc(uc_reg_write(uc, UC_X86_REG_RDI, &edi), "write edi");

  RunResult rr;
  uc_hook hookHandle{};
  auto hookCb = +[](uc_engine *, std::uint64_t address, std::uint32_t,
                    void *userData) {
    static_cast<std::vector<std::uint64_t> *>(userData)->push_back(address);
  };
  requireUc(uc_hook_add(uc, &hookHandle, UC_HOOK_CODE, (void *) hookCb,
                       &rr.trace, kCodePage, kCodePage + kPageSize - 1),
           "hook code");

  requireUc(uc_emu_start(uc, kFnAddr, kReturnSentinel, 0, 0), "uc_emu_start");

  std::uint64_t eax = 0;
  requireUc(uc_reg_read(uc, UC_X86_REG_RAX, &eax), "read eax");
  rr.result = static_cast<std::int32_t>(eax);
  uc_close(uc);
  return rr;
}

// JIT-executes `fnName(mem, regs)` with EDI=n already set, returns EAX.
std::int32_t jitRun(const std::string &irText, const std::string &fnName,
                    std::int32_t n) {
  static std::once_flag jitInitFlag;
  std::call_once(jitInitFlag, [] {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
  });

  auto ctx = std::make_unique<llvm::LLVMContext>();
  llvm::SMDiagnostic err;
  auto mod =
          llvm::parseIR(llvm::MemoryBufferRef(irText, "sum_to_n"), err, *ctx);
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
  std::vector<std::uint8_t> regs(320, 0); // room for the new XMM offsets too
  auto setReg32 = [&](std::uint32_t offset, std::int32_t value) {
    std::memcpy(regs.data() + offset, &value, sizeof(value));
  };
  auto setReg64 = [&](std::uint32_t offset, std::uint64_t value) {
    std::memcpy(regs.data() + offset, &value, sizeof(value));
  };
  setReg32(dragon::x86::kOffRDI, n);
  // Unlike obfush_repro's straight-line `compute` (no memory operands at
  // all), sum_to_n genuinely spills to the stack (`movsd [rsp-0x10],
  // xmm1`, real -O2 register pressure from CFLOW_BRANCHING's inflated
  // body) - its LOAD/STORE ops compute addresses as RSP + a (possibly
  // negative) displacement. Leaving RSP at 0 wraps a negative displacement
  // into a huge unsigned offset into `mem`, corrupting the heap well
  // outside the vector's actual allocation. Point it at the middle of
  // `mem` so displacements in either direction stay in bounds.
  setReg64(dragon::x86::kOffRSP, kPageSize / 2);

  fnPtr(mem.data(), regs.data());

  std::int32_t eax = 0;
  std::memcpy(&eax, regs.data() + dragon::x86::kOffRAX, sizeof(eax));
  return eax;
}

// Traces, lifts, optimizes and JIT-verifies sum_to_n(n) for one concrete n.
void runOneTrace(dragon::Disassembler &dis, std::int32_t n) {
  RunResult traced = runNative(n);
  std::cout << std::format(
          "Native trace: sum_to_n({}) = {} (expect {}), {} instructions "
          "executed.\n",
          n, traced.result, n * (n + 1) / 2, traced.trace.size());

  // Re-decode fresh at *every* traced address (see the file header comment
  // for why a single upfront linear sweep is unsafe for this target).
  std::vector<dragon::Instruction> ordered;
  ordered.reserve(traced.trace.size());
  for (auto addr: traced.trace) {
    auto blk = dis.disassembleBlock(kSumToNBytes, kFnAddr, addr);
    if (blk.empty())
      throw std::runtime_error(std::format("no instruction decodes at {:#x}", addr));
    ordered.push_back(blk.front());
  }

  dragon::ignil::Block block = dis.liftTrace(ordered);

  dragon::llvmlifter::LLVMLifter lifter(dragon::Architecture::X86_64,
                                        std::format("sum_to_n_{}", n));
  const std::string fnName = std::format("sum_to_n_traced_{}", n);
  if (!lifter.liftBlock(block, fnName)) {
    std::cerr << "liftBlock failed\n";
    return;
  }

  const auto rawIr = lifter.ir();

  std::cout << "  --- Not optimized IR ---\n" << rawIr << "\n";


  lifter.runMem2Reg();
  lifter.runInstCombine();
  lifter.runGVN();
  lifter.runDCE();
  const auto optIr = lifter.ir();

  std::cout << std::format(
          "  IGNIL ops: {}   IR: {} chars raw -> {} chars optimized\n",
          block.size(), rawIr.size(), optIr.size());

  std::int32_t jitResult = jitRun(optIr, fnName, n);
  std::cout << std::format(
          "  JIT(optimized)(n={}) = {} - {}\n\n", n, jitResult,
          jitResult == traced.result ? "MATCHES native trace" : "MISMATCH");

  std::cout << "  --- optimized IR ---\n" << optIr << "\n";
}

} // namespace

int main() {
  printHeader("obfusheader_repro: sum_to_n real trace -> IGNIL -> LLVM IR");
  std::cout
          << "Traces the real, unmodified sum_to_n binary (the exact "
             "function\ndiscussed manually in Binary Ninja - opaque "
             "predicate + overlapping-\ninstruction anti-disassembly "
             "junk in its loop condition), lifts each\ntrace straight-line "
             "(no CFG/branch discovery - see the file header),\nand checks "
             "what LLVM's ordinary optimizer does with it.\n\n";

  dragon::Configuration cfg;
  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);

  printHeader("Trace 1: n = 10");
  runOneTrace(dis, 10);

  printHeader("Trace 2: n = 6 (different input, independent trace)");
  runOneTrace(dis, 6);

  return 0;
}
