#pragma once

// Shared pipeline helpers for the toy VM defined in ToyVM.hpp: assemble,
// run under Unicorn (capturing a trace), lift a trace to IGNIL/LLVM IR, and
// JIT-execute the result. Split out from ToyVM.hpp (which stays a pure VM
// *definition* - ISA, assembly source, sample program, memory layout) once
// three consumers (TestToyVM.cpp, toy_vm_devirtualization.cpp, and
// toy_vm_handler_recovery.cpp) needed the same Unicorn/LLVM-JIT plumbing -
// duplicating it a third time stopped being reasonable.
//
// -- Handler-level recovery vs. whole-trace flattening ----------------------
//
// TestToyVM.cpp / toy_vm_devirtualization.cpp capture ONE trace of an
// entire program run and flatten it into one function. That only proves
// the recovered code is correct for the exact path traced - useful as a
// first mechanics check, but NOT the technique the back.engineering posts
// describe. Guided symbolic execution lifts the *static* interpreter with
// mostly-symbolic state and discovers the CFG by resolving branch targets;
// a single concrete trace never has an unresolved branch to discover in
// the first place.
//
// `segmentTraceByHandler` below is a step closer, still without full
// symbolic execution: since we know this VM's structure (we wrote it - the
// dispatch chain's `je` targets are exactly the handler entry points, read
// off via `Instruction::branches()` rather than hardcoded), a single
// whole-program trace can be *segmented* into one episode per handler,
// and each handler lifted to its own function independently. Each
// recovered handler function is then generically correct for *any* input
// to that handler (see toy_vm_handler_recovery.cpp's verification with
// fresh values never seen in the original capture) - not just the specific
// numbers that happened to flow through it during the one traced run. This
// is the "profile/lift each handler once" idea from the VMProtect2 tooling
// posts, done via liftTrace()+liftBlock()+optimize() instead of a
// hand-written per-handler lifter. It is still not guided symbolic
// execution: we still need a *concrete* run of each handler to trace (no
// CFG discovery over unresolved/symbolic branches), and a handler with its
// own internal data-dependent branch would still only have one arm
// recovered from a single trace.

#include "ToyVM.hpp"

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
#include <string>
#include <vector>

namespace dragon::test::toyvm {

inline void requireUc(uc_err err, const char *what) {
  if (err != UC_ERR_OK)
    throw std::runtime_error(std::string(what) + ": " + uc_strerror(err));
}

inline std::vector<std::uint8_t> assembleInterpreter() {
  dragon::Configuration cfg;
  dragon::Assembler as(dragon::Architecture::X86_64, cfg);
  auto bytes = as.assemble(kInterpreterSource);
  if (bytes.empty())
    throw std::runtime_error("assembleInterpreter: assembly failed");
  return bytes;
}

inline std::vector<dragon::Instruction> decodeInterpreter(
        const std::vector<std::uint8_t> &code) {
  dragon::Configuration cfg;
  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  return dis.disassemble(code, kCodeBase);
}

// Runs `code` under Unicorn with `bytecode` as the VM program. If `trace`
// is non-null, records every address executed (in order, duplicates
// included - a real trace, not a set of unique addresses).
inline std::uint32_t runNative(const std::vector<std::uint8_t> &code,
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

// Replays `trace`'s addresses against a one-time decode of `code` to build
// the ordered instruction sequence liftTrace() needs.
inline std::vector<dragon::Instruction> instructionsForTrace(
        const std::vector<dragon::Instruction> &decoded,
        const std::vector<std::uint64_t> &trace) {
  std::map<std::uint64_t, dragon::Instruction> byAddr;
  for (auto &insn: decoded)
    byAddr.emplace(insn.address(), insn);

  std::vector<dragon::Instruction> ordered;
  ordered.reserve(trace.size());
  for (auto addr: trace)
    ordered.push_back(byAddr.at(addr));
  return ordered;
}

inline dragon::ignil::Block liftCapturedTrace(const std::vector<std::uint8_t> &code,
                                              const std::vector<std::uint64_t> &trace) {
  dragon::Configuration cfg;
  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  auto ordered = instructionsForTrace(decodeInterpreter(code), trace);
  return dis.liftTrace(ordered);
}

inline std::size_t countInstructions(llvm::Module &mod) {
  std::size_t n = 0;
  for (auto &fn: mod)
    for (auto it = llvm::inst_begin(fn), end = llvm::inst_end(fn); it != end; ++it)
      ++n;
  return n;
}

// JIT-executes `irText`'s `fnName` with rsi=kBytecodeBase, rdi=kStackBase,
// `bytecode` already written into the mem buffer at kBytecodeBase. Mirrors
// X86SemanticRoundTripFixture.h's runLifted().
inline std::uint32_t jitRun(const std::string &irText, const std::string &fnName,
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

// Generic JIT call for handler-recovery verification: caller supplies the
// full initial mem/regs buffers and reads back whatever it needs (a
// handler doesn't necessarily leave its result in EAX the way the
// whole-program function does - e.g. op_add's result lands at [rdi-4] in
// mem, not in a register).
inline void jitRunHandler(const std::string &irText, const std::string &fnName,
                          std::vector<std::uint8_t> &mem,
                          std::vector<std::uint8_t> &regs) {
  static std::once_flag jitInitFlag;
  std::call_once(jitInitFlag, [] {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
  });

  auto ctx = std::make_unique<llvm::LLVMContext>();
  llvm::SMDiagnostic err;
  auto mod = llvm::parseIR(llvm::MemoryBufferRef(irText, "toy_vm"), err, *ctx);
  if (!mod)
    throw std::runtime_error("jitRunHandler: parseIR failed: " + err.getMessage().str());

  auto jitOrErr = llvm::orc::LLJITBuilder().create();
  if (!jitOrErr)
    throw std::runtime_error("jitRunHandler: LLJITBuilder failed");
  auto jit = std::move(*jitOrErr);

  llvm::orc::ThreadSafeContext tsCtx(std::move(ctx));
  llvm::orc::ThreadSafeModule tsm(std::move(mod), tsCtx);
  if (auto e = jit->addIRModule(std::move(tsm)))
    throw std::runtime_error("jitRunHandler: addIRModule failed");

  auto symOrErr = jit->lookup(fnName);
  if (!symOrErr)
    throw std::runtime_error("jitRunHandler: lookup(" + fnName + ") failed");
  auto fnPtr = symOrErr->toPtr<void (*)(std::uint8_t *, std::uint8_t *)>();
  fnPtr(mem.data(), regs.data());
}

// -- Per-handler trace segmentation ------------------------------------------
//
// Names match the labels in kInterpreterSource, in the fixed order the
// dispatch chain tests them (op_push, op_add, op_sub, op_and, op_or,
// op_xor, op_halt) - see the file header for why this is a meaningfully
// different (and more honest) technique than flattening one whole-program
// trace.
inline const std::vector<std::string> &handlerNames() {
  static const std::vector<std::string> names = {
          "op_push", "op_add", "op_sub", "op_and", "op_or", "op_xor", "op_halt"};
  return names;
}

// Discovers each handler's entry address from the dispatch chain's `je`
// targets (Instruction::branches() - no hardcoded offsets), then segments
// `trace` into one episode per handler: every time execution reaches a
// handler's entry address, a new episode starts and collects instructions
// until the next dispatch re-entry (address == kCodeBase) or the trace
// ends. Only the *first* occurrence of each handler in the trace is kept -
// the dispatch fetch+compare chain itself is discarded entirely (it's
// generic bookkeeping, not part of any one handler's semantics).
//
// Returns a map keyed by handler name; a name is absent if that handler
// never executed in `trace` (e.g. a bytecode program that never uses XOR).
inline std::map<std::string, std::vector<dragon::Instruction>>
segmentTraceByHandler(const std::vector<dragon::Instruction> &decoded,
                      const std::vector<std::uint64_t> &trace) {
  std::map<std::uint64_t, dragon::Instruction> byAddr;
  for (auto &insn: decoded)
    byAddr.emplace(insn.address(), insn);

  std::vector<std::uint64_t> entryAddrs;
  for (auto &insn: decoded) {
    if (insn.isConditional() && insn.isJump() && !insn.branches().empty())
      entryAddrs.push_back(*insn.branches().begin());
  }

  const auto &names = handlerNames();
  std::map<std::uint64_t, std::string> nameForEntry;
  for (std::size_t i = 0; i < entryAddrs.size() && i < names.size(); ++i)
    nameForEntry[entryAddrs[i]] = names[i];

  std::map<std::string, std::vector<dragon::Instruction>> segments;
  std::string current;
  bool collecting = false;

  for (auto addr: trace) {
    if (addr == kCodeBase) {
      collecting = false; // back at dispatch entry - end any open episode
    }
    if (auto it = nameForEntry.find(addr); it != nameForEntry.end()) {
      if (segments.count(it->second)) {
        collecting = false; // already have this handler's first occurrence
      } else {
        current = it->second;
        collecting = true;
        segments[current] = {};
      }
    }
    if (collecting)
      segments[current].push_back(byAddr.at(addr));
  }
  return segments;
}

} // namespace dragon::test::toyvm
