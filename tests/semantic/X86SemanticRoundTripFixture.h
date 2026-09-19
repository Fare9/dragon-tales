#pragma once

// Emulation-based semantic round-trip: run raw x86-64 bytes under Unicorn to
// get "expected" CPU state, run the same bytes through dragon-tales'
// Disassembler -> X86IGNILLifter -> LLVMLifter -> ORC JIT to get "actual"
// state, and let the caller diff them.
//
// This is what would have caught ToDo/LifterGaps.md #1 (the mem-destination
// MOV/ADD/SUB/CMP forms silently producing zero IGNIL ops) automatically:
// the IGNIL-shape checks in TestX86IGNILLifter.cpp can confirm ops were
// *emitted*, but only actually running the lifted code proves the *values*
// they compute match real hardware.
//
// --- Memory model ---
// LLVMLifter's calling convention is `void sub_<addr>(i8* mem, i8* regs)`,
// where every LOAD/STORE indexes `mem` by the real, absolute effective
// address computed from register values (see LLVMLifter.cpp's
// `CreateGEP(int8, memPtr, r0())`). To keep that pointer arithmetic inside
// real, well-defined bounds (no base-relative pointer tricks), this fixture
// allocates memory buffers big enough to cover address 0 through
// kMemBase+kMemSize and just wastes the unused prefix - simpler and free of
// undefined behavior compared to offsetting the pointer to fake a base.
//
// --- Stopping Unicorn: the sentinel return-address trick ---
// Test code may branch to more than one `ret` (e.g. an if/else with a ret
// on each arm), so there's no single fixed "stop address" to pass to
// uc_emu_start. Standard fix: push a sentinel address as the return address
// before starting, and run until PC reaches it - every path's `ret` pops
// the same sentinel and jumps there, however it got there. Unicorn stops
// when PC would *become* that address, without needing it mapped/executable.
//
// `liftRet` intentionally does not model the stack pop a real `ret`
// performs (see the comment in X86IGNILLifter.cpp::liftRet) - the lifted
// function returns via a plain LLVM `ret void` once IGNIL RET is reached,
// so RSP on that side never sees the pop. To stay comparable, runUnicorn
// subtracts the 8 bytes back off its own final RSP reading before
// returning, undoing the sentinel pop's effect on that one register.

#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/llvm/LLVMLifter.hpp>

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>

#include <unicorn/unicorn.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace dragon::test {

// IGNIL register address space offsets (must match
// lib/disassemblers/x86/x86registers.def - the lifter's single source of
// truth for this layout).
namespace x86off {
constexpr std::uint32_t RAX = 0, RCX = 8, RDX = 16, RBX = 24;
constexpr std::uint32_t RSP = 32, RBP = 40, RSI = 48, RDI = 56;
constexpr std::uint32_t R8 = 64, R9 = 72, R10 = 80, R11 = 88;
constexpr std::uint32_t R12 = 96, R13 = 104, R14 = 112, R15 = 120;
constexpr std::uint32_t CF = 136, PF = 137, AF = 138, ZF = 139;
constexpr std::uint32_t SF = 140, OF = 141, DF = 142;
} // namespace x86off

constexpr std::size_t kRegsSize = 160;
using RegBuf = std::array<std::uint8_t, kRegsSize>;

constexpr std::uint64_t kCodeBase = 0x1000;
constexpr std::uint64_t kMemBase = 0x2000;
constexpr std::uint64_t kMemSize = 0x1000;
// Default RSP/RBP: mid-region, giving 0x800 bytes of headroom on both sides
// for the small positive/negative stack-relative displacements these tests
// use (e.g. [rbp-32]).
constexpr std::uint64_t kDefaultSP = kMemBase + 0x800;

inline void setReg64(RegBuf &regs, std::uint32_t offset, std::uint64_t value) {
  std::memcpy(regs.data() + offset, &value, sizeof(value));
}

inline std::uint64_t getReg64(const RegBuf &regs, std::uint32_t offset) {
  std::uint64_t value = 0;
  std::memcpy(&value, regs.data() + offset, sizeof(value));
  return value;
}

// x86 EFLAGS bit positions for the 7 flags IGNIL models (bit 1 is the
// always-1 reserved bit; every other flag - IF, TF, ... - is left at 0).
inline std::uint64_t packEflags(const RegBuf &regs) {
  std::uint64_t v = 0x2;
  if (regs[x86off::CF]) v |= 1u << 0;
  if (regs[x86off::PF]) v |= 1u << 2;
  if (regs[x86off::AF]) v |= 1u << 4;
  if (regs[x86off::ZF]) v |= 1u << 6;
  if (regs[x86off::SF]) v |= 1u << 7;
  if (regs[x86off::DF]) v |= 1u << 10;
  if (regs[x86off::OF]) v |= 1u << 11;
  return v;
}

inline void unpackEflags(std::uint64_t v, RegBuf &regs) {
  regs[x86off::CF] = (v >> 0) & 1;
  regs[x86off::PF] = (v >> 2) & 1;
  regs[x86off::AF] = (v >> 4) & 1;
  regs[x86off::ZF] = (v >> 6) & 1;
  regs[x86off::SF] = (v >> 7) & 1;
  regs[x86off::DF] = (v >> 10) & 1;
  regs[x86off::OF] = (v >> 11) & 1;
}

// Flags the comparison has to ignore. There used to be three - PF, AF and DF
// were UNDEF after every ALU op - but the lifter has modelled PF and AF since
// LifterGaps.md #7 round 4 and leaves DF alone as the hardware does, so
// masking them would only hide bugs. What is left is per instruction (AF
// after a logical op, everything but CF/OF after MUL), which is the business
// of the test that knows which instruction it ran - see `sweep()` in
// TestX86SemanticRoundTrip.cpp. Kept as a hook for the state-wide comparison.
inline void maskUnmodeledFlags(RegBuf &) {}

struct RoundTripResult {
  RegBuf regs{};
  std::vector<std::uint8_t> mem; // exactly [kMemBase, kMemBase + kMemSize)
};

// runUnicorn writes an 8-byte sentinel return address at [initialSP] so
// `ret` has somewhere valid to pop (see the file header comment) - real
// hardware leaves that as stack garbage after the pop, so it shows up in
// Unicorn's final memory image. The JIT side never had a reason to write
// it at all. It's an artifact of how this harness stops emulation, not
// something either lifter path is responsible for reproducing - mask it
// out of both sides before comparing, same idea as maskUnmodeledFlags.
inline void maskSentinelSlot(std::vector<std::uint8_t> &mem,
                             const RegBuf &initialRegs) {
  const std::uint64_t initialSP = getReg64(initialRegs, x86off::RSP);
  const std::uint64_t offset = initialSP - kMemBase;
  for (std::uint64_t i = 0; i < 8 && offset + i < mem.size(); ++i)
    mem[offset + i] = 0;
}

// A default initial register state: RSP/RBP at kDefaultSP, everything else
// (including flags) zeroed. Tests copy this and override what they need.
inline RegBuf defaultInitialRegs() {
  RegBuf regs{};
  setReg64(regs, x86off::RSP, kDefaultSP);
  setReg64(regs, x86off::RBP, kDefaultSP);
  return regs;
}

namespace detail {

inline const std::array<std::pair<int, std::uint32_t>, 16> &gprRegMap() {
  static const std::array<std::pair<int, std::uint32_t>, 16> map = {{
          {UC_X86_REG_RAX, x86off::RAX}, {UC_X86_REG_RCX, x86off::RCX},
          {UC_X86_REG_RDX, x86off::RDX}, {UC_X86_REG_RBX, x86off::RBX},
          {UC_X86_REG_RSP, x86off::RSP}, {UC_X86_REG_RBP, x86off::RBP},
          {UC_X86_REG_RSI, x86off::RSI}, {UC_X86_REG_RDI, x86off::RDI},
          {UC_X86_REG_R8, x86off::R8},   {UC_X86_REG_R9, x86off::R9},
          {UC_X86_REG_R10, x86off::R10}, {UC_X86_REG_R11, x86off::R11},
          {UC_X86_REG_R12, x86off::R12}, {UC_X86_REG_R13, x86off::R13},
          {UC_X86_REG_R14, x86off::R14}, {UC_X86_REG_R15, x86off::R15},
  }};
  return map;
}

inline void requireUc(uc_err err, const char *what) {
  if (err != UC_ERR_OK)
    throw std::runtime_error(std::string(what) + ": " + uc_strerror(err));
}

} // namespace detail

// Arbitrary, unmapped address, far from the code/stack regions - never
// actually executed, just used as a PC value for uc_emu_start to stop at.
constexpr std::uint64_t kReturnSentinel = 0x7fff0000ULL;

// Runs `code` on real (emulated) x86-64 hardware via Unicorn. Every path
// through `code` must eventually execute a `ret` - see the file header
// comment for how that's turned into a well-defined stopping point.
inline RoundTripResult runUnicorn(std::span<const std::uint8_t> code,
                                  const RegBuf &initialRegs,
                                  const std::vector<std::uint8_t> &initialMem) {
  uc_engine *uc = nullptr;
  detail::requireUc(uc_open(UC_ARCH_X86, UC_MODE_64, &uc), "uc_open");

  constexpr std::uint64_t kCodePageSize = 0x1000;
  detail::requireUc(uc_mem_map(uc, kCodeBase, kCodePageSize, UC_PROT_ALL), "map code");
  detail::requireUc(uc_mem_write(uc, kCodeBase, code.data(), code.size()), "write code");

  detail::requireUc(uc_mem_map(uc, kMemBase, kMemSize, UC_PROT_ALL), "map mem");
  if (!initialMem.empty())
    detail::requireUc(uc_mem_write(uc, kMemBase, initialMem.data(), initialMem.size()),
                      "write initial mem");

  for (auto [ucReg, offset]: detail::gprRegMap()) {
    std::uint64_t value = getReg64(initialRegs, offset);
    detail::requireUc(uc_reg_write(uc, ucReg, &value), "write gpr");
  }
  std::uint64_t eflags = packEflags(initialRegs);
  detail::requireUc(uc_reg_write(uc, UC_X86_REG_EFLAGS, &eflags), "write eflags");

  // Write the sentinel return address at [initialSP] *without* moving RSP -
  // initialSP is taken to mean "RSP as the callee already sees it", i.e. as
  // if a `call` had already pushed a return address immediately below it.
  // Crucially this must not touch the RSP register: runLifted() gives the
  // JIT'd side that exact same initialSP with no adjustment, so if Unicorn's
  // real RSP started anywhere else the two sides would run the body (and
  // its push/pop rbp pair) from different stack depths and never agree on
  // RSP at the end, even though both sides handled push/pop internally
  // consistently.
  const std::uint64_t initialSP = getReg64(initialRegs, x86off::RSP);
  detail::requireUc(uc_mem_write(uc, initialSP, &kReturnSentinel, sizeof(kReturnSentinel)),
                    "write ret sentinel");

  {
    uc_err err = uc_emu_start(uc, kCodeBase, kReturnSentinel, 0, 0);
    if (err != UC_ERR_OK) {
      std::uint64_t pc = 0, sp = 0;
      uc_reg_read(uc, UC_X86_REG_RIP, &pc);
      uc_reg_read(uc, UC_X86_REG_RSP, &sp);
      throw std::runtime_error(std::string("uc_emu_start: ") + uc_strerror(err) +
                               std::format(" at RIP=0x{:x} RSP=0x{:x}", pc, sp));
    }
  }

  RoundTripResult result;
  result.mem.resize(kMemSize);
  for (auto [ucReg, offset]: detail::gprRegMap()) {
    std::uint64_t value = 0;
    detail::requireUc(uc_reg_read(uc, ucReg, &value), "read gpr");
    setReg64(result.regs, offset, value);
  }
  // Undo the sentinel `ret`'s pop so RSP stays comparable with the lifted
  // side, which never modeled it (see file header comment).
  setReg64(result.regs, x86off::RSP, getReg64(result.regs, x86off::RSP) - 8);

  std::uint64_t eflagsOut = 0;
  detail::requireUc(uc_reg_read(uc, UC_X86_REG_EFLAGS, &eflagsOut), "read eflags");
  unpackEflags(eflagsOut, result.regs);
  detail::requireUc(uc_mem_read(uc, kMemBase, result.mem.data(), kMemSize), "read mem");

  uc_close(uc);
  return result;
}

// Runs `code` through the real dragon-tales pipeline (disassemble -> lift
// to IGNIL -> lift to LLVM IR) and JIT-executes the result via ORC.
inline RoundTripResult runLifted(std::span<const std::uint8_t> code,
                                 const RegBuf &initialRegs,
                                 const std::vector<std::uint8_t> &initialMem,
                                 std::uint64_t entryAddr = kCodeBase) {
  static std::once_flag jitInitFlag;
  std::call_once(jitInitFlag, [] {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
  });

  dragon::Configuration cfg;
  cfg.disassemblyStrategy = dragon::DisassemblyStrategy::LINEAR_SWEEP;
  cfg.syntax = dragon::Syntax::INTEL;

  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  dragon::Graph g(dragon::Architecture::X86_64, cfg);
  dis.disassemble(std::vector<std::uint8_t>(code.begin(), code.end()), entryAddr, g);

  dragon::llvmlifter::LLVMLifter llvml(dragon::Architecture::X86_64, "semantic_rt");

  bool lifted = false;
  for (auto &fn: g.functions()) {
    if (fn.startAddress() == entryAddr) {
      lifted = llvml.lift(fn, g);
      break;
    }
  }
  if (!lifted)
    throw std::runtime_error("runLifted: LLVMLifter::lift() failed");

  const std::string irText = llvml.ir();

  auto ctx = std::make_unique<llvm::LLVMContext>();
  llvm::SMDiagnostic err;
  auto mod = llvm::parseIR(
          llvm::MemoryBufferRef(irText, "semantic_rt"), err, *ctx);
  if (!mod)
    throw std::runtime_error("runLifted: parseIR failed: " + err.getMessage().str());

  auto jitOrErr = llvm::orc::LLJITBuilder().create();
  if (!jitOrErr)
    throw std::runtime_error("runLifted: LLJITBuilder failed: " +
                             llvm::toString(jitOrErr.takeError()));
  auto jit = std::move(*jitOrErr);

  llvm::orc::ThreadSafeContext tsCtx(std::move(ctx));
  llvm::orc::ThreadSafeModule tsm(std::move(mod), tsCtx);
  if (auto e = jit->addIRModule(std::move(tsm)))
    throw std::runtime_error("runLifted: addIRModule failed: " + llvm::toString(std::move(e)));

  const std::string fnName = std::format("sub_{:x}", entryAddr);
  auto symOrErr = jit->lookup(fnName);
  if (!symOrErr)
    throw std::runtime_error("runLifted: lookup(" + fnName +
                             ") failed: " + llvm::toString(symOrErr.takeError()));

  auto fnPtr = symOrErr->toPtr<void (*)(std::uint8_t *, std::uint8_t *)>();

  std::vector<std::uint8_t> memBuf(kMemBase + kMemSize, 0);
  if (!initialMem.empty())
    std::copy(initialMem.begin(), initialMem.end(), memBuf.begin() + kMemBase);

  RegBuf regs = initialRegs;
  fnPtr(memBuf.data(), regs.data());

  RoundTripResult result;
  result.regs = regs;
  result.mem.assign(memBuf.begin() + kMemBase, memBuf.end());
  return result;
}

} // namespace dragon::test
