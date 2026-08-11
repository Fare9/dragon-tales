// Handler-level devirtualization: segments ONE captured whole-program trace
// into one episode per VM handler and lifts+optimizes each independently,
// producing a small, standalone, generically-correct native function per
// VM opcode - rather than one function flattened for a single traced path
// (that's toy_vm_devirtualization.cpp). See
// tests/toy-vm/ToyVMHarness.hpp's header comment for why this is a step
// closer to (but still not) the guided symbolic execution the
// back.engineering posts describe, and
// tests/toy-vm/TestToyVMHandlerRecovery.cpp for the Catch2 assertions that
// verify each recovered handler against fresh inputs never seen in the
// original trace.
//
// -- Stack-pointer concretization ("preparation" + "lowering", scoped to
// this file only) -----------------------------------------------------------
//
// The Tencent/Themida posts concretize RSP to an arbitrary constant before
// lifting/optimizing, purely so RSP-relative stack addresses fold to
// literal integers - LLVM's ordinary BasicAA already knows how to reason
// precisely about "same base, different constant offset", so this alone
// unlocks free load-forwarding/DSE across the virtual stack with zero
// custom passes. They then reverse it ("preparation... before lowering the
// SSA IR back to native") so the result is a normal function again, not one
// hardcoded to an arbitrary constant.
//
// Both halves are demonstrated below, purely at the IGNIL/LLVM-IR level,
// with no changes to dragon-tales itself:
//   - concretizeStackPointer(): an IGNIL-level trick - prepend a synthetic
//     `RDI = Const(value)` op to a handler's block before lifting. No new
//     API needed; ignil::Block already supports building one op-by-op.
//   - rewriteStackPointerBack(): the companion LLVM-IR-level pass - after
//     optimization, find every load/store address (and the RDI register
//     write-back) that resolved to the concretized constant, and rewrite
//     it back in terms of the real RDI parameter. This one - unlike the
//     concretization trick - is real, generic LLVM IR surgery that would
//     make sense as a dragon-tales API if this direction gets pursued
//     further; here it's deliberately simplified (targets exactly the two
//     patterns this VM's handlers produce) rather than a fully general
//     pass, per the scope of this experiment.

#include "../../tests/toy-vm/ToyVMHarness.hpp"

#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Instructions.h>

#include <cstring>
#include <format>
#include <iostream>

using namespace dragon::test::toyvm;

namespace {

void printHeader(const std::string &title) {
  std::cout << "\n" << std::string(80, '=') << "\n  " << title << "\n"
            << std::string(80, '=') << "\n";
}

void setReg64(std::vector<std::uint8_t> &regs, std::uint32_t off, std::uint64_t v) {
  std::memcpy(regs.data() + off, &v, sizeof(v));
}
std::uint64_t getReg64(std::vector<std::uint8_t> &regs, std::uint32_t off) {
  std::uint64_t v = 0;
  std::memcpy(&v, regs.data() + off, sizeof(v));
  return v;
}
void setMem32(std::vector<std::uint8_t> &mem, std::uint64_t addr, std::uint32_t v) {
  std::memcpy(mem.data() + addr, &v, sizeof(v));
}
std::uint32_t getMem32(std::vector<std::uint8_t> &mem, std::uint64_t addr) {
  std::uint32_t v = 0;
  std::memcpy(&v, mem.data() + addr, sizeof(v));
  return v;
}

constexpr std::uint64_t kFreshData  = 0x6000;
constexpr std::uint64_t kFreshStack = 0x7000;
constexpr std::size_t kFreshMemSize = 0x8000;

// Runs the same fresh-input checks TestToyVMHandlerRecovery.cpp verifies -
// used both on the plain recovered IR and, later, on the
// concretize->optimize->rewrite-back round-tripped IR, to prove the
// round trip didn't quietly break generality.
bool verifyHandlers(const std::string &ir) {
  bool allOk = true;
  auto note = [&](bool ok) { allOk = allOk && ok; return ok ? "OK" : "MISMATCH"; };

  {
    std::vector<std::uint8_t> mem(kFreshMemSize, 0), regs(160, 0);
    setMem32(mem, kFreshData, 777);
    setReg64(regs, regoff::RSI, kFreshData);
    setReg64(regs, regoff::RDI, kFreshStack);
    jitRunHandler(ir, "op_push", mem, regs);
    bool ok = getMem32(mem, kFreshStack) == 777 &&
             getReg64(regs, regoff::RDI) == kFreshStack + 4 &&
             getReg64(regs, regoff::RSI) == kFreshData + 4;
    std::cout << std::format("  op_push(777)              -> [rdi]={} - {}\n",
                             getMem32(mem, kFreshStack), note(ok));
  }

  auto checkBinOp = [&](const char *name, std::uint32_t a, std::uint32_t b,
                       std::uint32_t expected) {
    std::vector<std::uint8_t> mem(kFreshMemSize, 0), regs(160, 0);
    setMem32(mem, kFreshStack, a);
    setMem32(mem, kFreshStack + 4, b);
    setReg64(regs, regoff::RDI, kFreshStack + 8);
    jitRunHandler(ir, name, mem, regs);
    auto got = getMem32(mem, kFreshStack);
    std::cout << std::format("  {}({}, {}) = {} (expected {}) - {}\n", name, a, b,
                             got, expected, note(got == expected));
  };

  checkBinOp("op_add", 45, 17, 45u + 17u);
  checkBinOp("op_sub", 100, 23, 100u - 23u);
  checkBinOp("op_and", 12, 10, 12u & 10u);
  checkBinOp("op_or", 12, 10, 12u | 10u);
  checkBinOp("op_xor", 12, 10, 12u ^ 10u);

  {
    std::vector<std::uint8_t> mem(kFreshMemSize, 0), regs(160, 0);
    setMem32(mem, kFreshStack, 555);
    setReg64(regs, regoff::RDI, kFreshStack + 4);
    jitRunHandler(ir, "op_halt", mem, regs);
    std::uint32_t eax = 0;
    std::memcpy(&eax, regs.data() + regoff::RAX, sizeof(eax));
    std::cout << std::format("  op_halt([rdi-4]=555)       -> eax={} - {}\n", eax,
                             note(eax == 555));
  }

  return allOk;
}

// -- Stack-pointer concretization (IGNIL level) ------------------------------

// The arbitrary constant handlers' RDI gets pinned to before optimization,
// and the byte range around it every handler's stack traffic can fall
// into (our handlers only ever touch [rdi-8, rdi+4), but padded generously
// since op_push's [rdi] alone still needs to land inside).
constexpr std::uint64_t kConcreteRdi   = 0x9000 + 0x80;
constexpr std::uint64_t kConcreteBase  = 0x9000;
constexpr std::uint64_t kConcreteRange = 0x100;

dragon::ignil::Block concretizeStackPointer(const dragon::ignil::Block &block,
                                            std::uint64_t value) {
  dragon::ignil::Block prepped(block.startAddress());
  dragon::ignil::Op setRdi;
  setRdi.opcode = dragon::ignil::Opcode::COPY;
  setRdi.dst = dragon::ignil::Value(dragon::ignil::Reg{regoff::RDI, 64, "RDI"});
  setRdi.srcs = {dragon::ignil::Value(dragon::ignil::Const{value, 64})};
  prepped.append(setRdi);
  for (const auto &op: block.ops())
    prepped.append(op);
  return prepped;
}

// -- Rewrite-back (LLVM IR level) --------------------------------------------

// Undoes the concretization in one already-optimized function: rewrites
// (a) every load/store address that resolved to `mem + LITERAL` with
// LITERAL inside [kConcreteBase, kConcreteBase+kConcreteRange) back to
// `mem + (real_rdi + (LITERAL - kConcreteBase))`, and (b) the RDI
// register write-back (`store i64 LITERAL, ptr regs+56`) back to
// `store i64 (real_rdi + (LITERAL - kConcreteBase)), ptr regs+56`. "real_rdi"
// is reconstructed with a fresh load from the regs parameter the first time
// either pattern is found - the original load may well have been DCE'd
// away entirely once nothing observable still depended on it.
//
// Deliberately targets exactly these two patterns (this VM's handlers never
// produce anything else stack-related) rather than being a fully general
// pass - see this file's header comment.
void rewriteStackPointerBack(llvm::Function &fn, const llvm::DataLayout &dl) {
  llvm::Value *memArg  = fn.getArg(0);
  llvm::Value *regsArg = fn.getArg(1);
  llvm::Value *realRdi = nullptr;
  llvm::IRBuilder<> entryBuilder(&fn.getEntryBlock(),
                                 fn.getEntryBlock().getFirstInsertionPt());
  auto ensureRealRdi = [&]() {
    if (!realRdi) {
      auto *ptr = entryBuilder.CreateGEP(entryBuilder.getInt8Ty(), regsArg,
                                         entryBuilder.getInt32(regoff::RDI));
      realRdi = entryBuilder.CreateLoad(entryBuilder.getInt64Ty(), ptr, "real_rdi");
    }
    return realRdi;
  };

  std::vector<llvm::Instruction *> memOps;
  for (auto &inst: llvm::instructions(fn))
    if (llvm::isa<llvm::LoadInst>(inst) || llvm::isa<llvm::StoreInst>(inst))
      memOps.push_back(&inst);

  for (auto *inst: memOps) {
    // (a) address rewriting: does this load/store's pointer resolve to
    // mem + a literal inside the concretized range?
    unsigned ptrIdx = llvm::isa<llvm::LoadInst>(inst)
                              ? llvm::LoadInst::getPointerOperandIndex()
                              : llvm::StoreInst::getPointerOperandIndex();
    llvm::Value *ptrOperand = inst->getOperand(ptrIdx);
    llvm::APInt addrOffset(64, 0);
    llvm::Value *base =
            ptrOperand->stripAndAccumulateConstantOffsets(dl, addrOffset, true);
    if (base == memArg) {
      std::uint64_t off = addrOffset.getZExtValue();
      if (off >= kConcreteBase && off < kConcreteBase + kConcreteRange) {
        auto delta = static_cast<std::int64_t>(off) -
                     static_cast<std::int64_t>(kConcreteRdi);
        llvm::IRBuilder<> b(inst);
        llvm::Value *newOffset = b.CreateAdd(ensureRealRdi(), b.getInt64(delta));
        inst->setOperand(ptrIdx, b.CreateGEP(b.getInt8Ty(), memArg, newOffset));
      }
    }

    // (b) RDI write-back: `store i64 LITERAL, ptr (regs + regoff::RDI)`.
    auto *store = llvm::dyn_cast<llvm::StoreInst>(inst);
    if (!store)
      continue;
    llvm::APInt storePtrOffset(64, 0);
    llvm::Value *storeBase = store->getPointerOperand()->stripAndAccumulateConstantOffsets(
            dl, storePtrOffset, true);
    if (storeBase != regsArg || storePtrOffset.getZExtValue() != regoff::RDI)
      continue;
    auto *storedConst = llvm::dyn_cast<llvm::ConstantInt>(store->getValueOperand());
    if (!storedConst)
      continue;
    std::uint64_t v = storedConst->getZExtValue();
    if (v < kConcreteBase || v >= kConcreteBase + kConcreteRange)
      continue;
    auto delta = static_cast<std::int64_t>(v) - static_cast<std::int64_t>(kConcreteRdi);
    llvm::IRBuilder<> b(store);
    store->setOperand(0, b.CreateAdd(ensureRealRdi(), b.getInt64(delta)));
  }
}

} // namespace

int main() {
  printHeader("Toy VM Handler Recovery Demo (per-handler segmentation)");
  std::cout << "Captures ONE whole-program trace, then segments it into one "
               "episode per\nVM handler (dispatch fetch+compare chain "
               "discarded - see ToyVMHarness.hpp for\nhow entry addresses "
               "are discovered from the dispatch chain's `je` targets, no\n"
               "hardcoded offsets), lifts each handler independently, and "
               "optimizes all of\nthem together in one pass.\n";

  auto code = assembleInterpreter();
  std::vector<std::uint64_t> trace;
  auto nativeResult = runNative(code, program(), &trace);
  std::cout << std::format(
          "\nNative result: {} (expected {}). Trace: {} instructions.\n",
          nativeResult, kExpectedResult, trace.size());

  auto decoded = decodeInterpreter(code);
  auto segments = segmentTraceByHandler(decoded, trace);

  // Lift each handler's segment to IGNIL up front so the segmentation
  // table below can show both the x86 instruction count and the IGNIL op
  // count it expanded to (one x86 instruction is often several IGNIL ops -
  // e.g. an ADD needs its own flag computation).
  dragon::Configuration cfg;
  dragon::Disassembler dis(dragon::Architecture::X86_64, cfg);
  std::map<std::string, dragon::ignil::Block> blocks;
  for (const auto &[name, insns]: segments)
    blocks.emplace(name, dis.liftTrace(insns));

  printHeader("Segmentation");
  for (const auto &name: handlerNames()) {
    auto segIt = segments.find(name);
    auto blockIt = blocks.find(name);
    std::cout << std::format(
            "  {:8s} -> {:2d} x86 instructions -> {:3d} IGNIL ops\n", name,
            segIt == segments.end() ? 0 : segIt->second.size(),
            blockIt == blocks.end() ? 0 : blockIt->second.size());
  }

  dragon::llvmlifter::LLVMLifter lifter(dragon::Architecture::X86_64,
                                        "toy_vm_handlers");
  for (const auto &[name, block]: blocks) {
    if (!lifter.liftBlock(block, name)) {
      std::cerr << "liftBlock failed for " << name << "\n";
      return 1;
    }
  }

  const auto rawCount = countInstructions(lifter.module());
  lifter.runMem2Reg();
  lifter.runInstCombine();
  lifter.runGVN();
  lifter.runDCE();
  const auto optimizedCount = countInstructions(lifter.module());
  auto ir = lifter.ir();

  printHeader("Recovered handlers (full optimized LLVM IR, all 7 in one module)");
  std::cout << std::format(
          "Raw: {} LLVM instructions across all handlers. Optimized: {} "
          "({}x smaller).\n\n",
          rawCount, optimizedCount, rawCount / std::max<std::size_t>(optimizedCount, 1));
  std::cout << ir << "\n";

  printHeader("Verification with fresh inputs (never seen in the original trace)");
  verifyHandlers(ir);

  // ---------------------------------------------------------------------
  // Stack-pointer concretization: prepare -> optimize -> rewrite back.
  // ---------------------------------------------------------------------

  printHeader("Stack-pointer concretization (IGNIL prep -> optimize -> LLVM rewrite-back)");
  std::cout << std::format(
          "Prepending `RDI = Const(0x{:x})` to every handler's IGNIL block "
          "before lifting,\nso every RDI-relative stack address folds to a "
          "literal integer during\noptimization - the same free BasicAA "
          "reasoning that already explains\ntoy_vm_devirtualization.cpp's "
          "2300->74 instruction collapse, but without needing\na concrete "
          "execution trace to get there.\n\n",
          kConcreteRdi);

  dragon::llvmlifter::LLVMLifter concreteLifter(dragon::Architecture::X86_64,
                                                "toy_vm_handlers_concrete");
  for (const auto &[name, block]: blocks) {
    auto concretized = concretizeStackPointer(block, kConcreteRdi);
    if (!concreteLifter.liftBlock(concretized, name)) {
      std::cerr << "liftBlock failed for concretized " << name << "\n";
      return 1;
    }
  }
  concreteLifter.runMem2Reg();
  concreteLifter.runInstCombine();
  concreteLifter.runGVN();
  concreteLifter.runDCE();

  std::cout << "--- Optimized IR with RDI concretized (literal stack addresses) ---\n"
            << concreteLifter.ir() << "\n";

  const auto &dl = concreteLifter.module().getDataLayout();
  for (auto &fn: concreteLifter.module())
    if (!fn.isDeclaration())
      rewriteStackPointerBack(fn, dl);
  concreteLifter.runDCE(); // clean up whatever the rewrite left dangling

  auto rewrittenIr = concreteLifter.ir();
  std::cout << "--- After rewrite-back (RDI is a real parameter again) ---\n"
            << rewrittenIr << "\n";

  printHeader("Verification of the round-tripped (concretize -> optimize -> "
             "rewrite-back) IR");
  bool roundTripOk = verifyHandlers(rewrittenIr);
  std::cout << std::format(
          "\nRound trip preserved correctness for fresh inputs: {}\n",
          roundTripOk ? "YES" : "NO");

  return 0;
}
