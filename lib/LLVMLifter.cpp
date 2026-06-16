#include <dragon/lifters/llvm/LLVMLifter.hpp>

#include <format>
#include <utility>

#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/DCE.h>
#include <llvm/Transforms/Scalar/GVN.h>
#include <llvm/Transforms/Utils/Mem2Reg.h>

bool dragon::llvmlifter::LLVMLifter::lift(const Function &fn,
                                          const Graph &graph) {
  // Generate a function without return value, and without parameters
  // we have not extracted information about that yet...
  // The name is the address of the function.
  std::string name = std::format("sub_{:x}", fn.startAddress());
  auto *ptrTy = llvm::PointerType::getUnqual(*ctx_);
  auto *voidTy = llvm::Type::getVoidTy(*ctx_);
  auto *FnType = llvm::FunctionType::get(voidTy, {ptrTy, ptrTy}, false);
  auto *llfn = llvm::Function::Create(
          FnType, llvm::GlobalValue::ExternalLinkage, name, *module_);
  llfn->getArg(0)->setName("mem");
  llfn->getArg(1)->setName("regs");

  auto *memPtr  = llfn->getArg(0);
  auto *regsPtr = llfn->getArg(1);

  auto bbMap = buildBlockMap(llfn, &fn, graph);

  if (!bbMap)
    return false;

  auto regFile = buildRegFile(llfn, &fn, graph);

  for (const auto &[addr, block]: fn.blocks()) {
    const auto *ib = graph.getIGNILBlock(addr);
    if (!ib)
      continue;
    TempMap temps{}; // fresh per block — Temp ids don't cross block boundaries
    liftBlock(ib, llfn, &regFile, &temps, memPtr, regsPtr, bbMap.value());
  }
  return true;
}
dragon::llvmlifter::LLVMLifter::RegFile
dragon::llvmlifter::LLVMLifter::buildRegFile(llvm::Function *llfn,
                                             const Function *fn,
                                             const Graph &graph) const {
  RegFile regFile{};
  std::map<std::pair<uint32_t, ignil::Width>, std::string_view> regNames;
  // Lambda used later to check if a value contains a register.
  // in that case, we will keep it for the alloca instruction
  auto collect = [&](const ignil::Value &v) {
    if (std::holds_alternative<ignil::Reg>(v)) {
      const auto &reg = std::get<ignil::Reg>(v);
      regFile[reg.offset].try_emplace(reg.width, nullptr);
      if (!reg.name.empty())
        regNames[{reg.offset, reg.width}] = reg.name;
    }
  };

  // go through every block of the function
  for (const auto &[addr, block]: fn->blocks()) {
    // retrieve the lifted version
    const auto *ignilBlock = graph.getIGNILBlock(addr);
    if (!ignilBlock)
      continue;
    // Get through each one of the instructions
    // we try to collect the registers.
    for (const auto &op: ignilBlock->ops()) {
      if (op.dst.has_value())
        collect(op.dst.value());
      for (const auto &src: op.srcs)
        collect(src);
    }
  }

  llvm::IRBuilder<> builder(&llfn->getEntryBlock(),
                            llfn->getEntryBlock().begin());
  for (auto &[offset, widthMap]: regFile) {
    for (auto &[width, slot]: widthMap) {
      // If we can provide a better naming...s
      auto it = regNames.find({offset, width});
      std::string name = (it != regNames.end())
                                 ? std::string(it->second)
                                 : "reg_" + std::to_string(offset) + "_" +
                                           std::to_string(width) + "b";
      slot = builder.CreateAlloca(llvm::IntegerType::get(*ctx_, width), nullptr,
                                  name);
    }
  }

  return regFile;
}
std::optional<std::map<std::uint64_t, llvm::BasicBlock *>>
dragon::llvmlifter::LLVMLifter::buildBlockMap(llvm::Function *llfn,
                                              const Function *fn,
                                              const Graph &graph) const {
  std::map<std::uint64_t, llvm::BasicBlock *> addrBlock;

  for (const auto &[addr, block]: fn->blocks()) {
    // If a block cannot be obtained from IGNIL, it doesn't have
    // any sense to lift the function to LLVM IR, since a block
    // would be lost.
    if (!graph.getIGNILBlock(addr))
      return std::nullopt;

    addrBlock[addr] =
            llvm::BasicBlock::Create(*ctx_, "bb_" + std::to_string(addr), llfn);
  }

  return addrBlock;
}
void dragon::llvmlifter::LLVMLifter::liftBlock(
        const ignil::Block *ib, llvm::Function *llfn, RegFile *regs,
        TempMap *temps, llvm::Value *memPtr, llvm::Value *regsPtr,
        const std::map<std::uint64_t, llvm::BasicBlock *> &bbMap) {
  llvm::IRBuilder<> builder(bbMap.at(ib->startAddress()));

  for (const auto &op: ib->ops())
    liftOp(op, *regs, *temps, memPtr, regsPtr, builder, bbMap);
}
void dragon::llvmlifter::LLVMLifter::liftOp(
        const ignil::Op &op, RegFile &regs, TempMap &temps, llvm::Value *memPtr,
        llvm::Value *regsPtr, llvm::IRBuilder<> &builder,
        const std::map<std::uint64_t, llvm::BasicBlock *> &bbMap) {

  // Store a result into dst: Temp → SSA table, Reg → alloca + sub-reg merge.
  auto emit = [&](llvm::Value *val) {
    if (!op.dst)
      return;
    if (const auto *t = std::get_if<ignil::Temp>(&*op.dst)) {
      // If it is a temporal value, it will be used
      // for SSA
      temps[t->id] = val;
    } else if (const auto *r = std::get_if<ignil::Reg>(&*op.dst)) {
      auto *slotTy = llvm::IntegerType::get(*ctx_, r->width);
      if (val->getType() != slotTy)
        val = builder.CreateZExt(val, slotTy);
      builder.CreateStore(val, regs.at(r->offset).at(r->width));
      mergeIntoParent(r->offset, r->width, val, regs, builder);
    }
  };

  // Lambda functions to quickly manage the common operands of the operations
  auto r0 = [&] { return resolve(op.srcs[0], regs, temps, builder); };
  auto r1 = [&] { return resolve(op.srcs[1], regs, temps, builder); };
  auto r2 = [&] { return resolve(op.srcs[2], regs, temps, builder); };
  // Lambda to quickly retrieve the integer type of the destination
  auto dstTy = [&] {
    return llvm::IntegerType::get(*ctx_, ignil::widthOf(*op.dst));
  };

  switch (op.opcode) {
    // --- Data movement ---
    case ignil::Opcode::COPY:
      emit(r0());
      break;

    case ignil::Opcode::LOAD: {
      auto *ptr = builder.CreateGEP(builder.getInt8Ty(), memPtr, r0());
      emit(builder.CreateLoad(dstTy(), ptr));
      break;
    }
    case ignil::Opcode::STORE: {
      auto *ptr = builder.CreateGEP(builder.getInt8Ty(), memPtr, r0());
      builder.CreateStore(r1(), ptr);
      break;
    }

    // --- Arithmetic ---
    case ignil::Opcode::ADD:
      emit(builder.CreateAdd(r0(), r1()));
      break;
    case ignil::Opcode::SUB:
      emit(builder.CreateSub(r0(), r1()));
      break;
    case ignil::Opcode::NEG:
      emit(builder.CreateNeg(r0()));
      break;
    case ignil::Opcode::MUL:
      emit(builder.CreateMul(r0(), r1()));
      break;
    case ignil::Opcode::SDIV:
      emit(builder.CreateSDiv(r0(), r1()));
      break;
    case ignil::Opcode::UDIV:
      emit(builder.CreateUDiv(r0(), r1()));
      break;
    case ignil::Opcode::SMOD:
      emit(builder.CreateSRem(r0(), r1()));
      break;
    case ignil::Opcode::UMOD:
      emit(builder.CreateURem(r0(), r1()));
      break;

    // --- Bitwise ---
    case ignil::Opcode::AND:
      emit(builder.CreateAnd(r0(), r1()));
      break;
    case ignil::Opcode::OR:
      emit(builder.CreateOr(r0(), r1()));
      break;
    case ignil::Opcode::XOR:
      emit(builder.CreateXor(r0(), r1()));
      break;
    case ignil::Opcode::NOT:
      emit(builder.CreateNot(r0()));
      break;
    case ignil::Opcode::SHL:
      emit(builder.CreateShl(r0(), r1()));
      break;
    case ignil::Opcode::SHR:
      emit(builder.CreateLShr(r0(), r1()));
      break;
    case ignil::Opcode::SAR:
      emit(builder.CreateAShr(r0(), r1()));
      break;

    // --- Width conversions ---
    case ignil::Opcode::ZEXT:
      emit(builder.CreateZExt(r0(), dstTy()));
      break;
    case ignil::Opcode::SEXT:
      emit(builder.CreateSExt(r0(), dstTy()));
      break;
    case ignil::Opcode::TRUNC:
      emit(builder.CreateTrunc(r0(), dstTy()));
      break;

    // --- Comparisons (always produce i1) ---
    case ignil::Opcode::EQ:
      emit(builder.CreateICmpEQ(r0(), r1()));
      break;
    case ignil::Opcode::NEQ:
      emit(builder.CreateICmpNE(r0(), r1()));
      break;
    case ignil::Opcode::ULT:
      emit(builder.CreateICmpULT(r0(), r1()));
      break;
    case ignil::Opcode::ULE:
      emit(builder.CreateICmpULE(r0(), r1()));
      break;
    case ignil::Opcode::UGT:
      emit(builder.CreateICmpUGT(r0(), r1()));
      break;
    case ignil::Opcode::UGE:
      emit(builder.CreateICmpUGE(r0(), r1()));
      break;
    case ignil::Opcode::SLT:
      emit(builder.CreateICmpSLT(r0(), r1()));
      break;
    case ignil::Opcode::SLE:
      emit(builder.CreateICmpSLE(r0(), r1()));
      break;
    case ignil::Opcode::SGT:
      emit(builder.CreateICmpSGT(r0(), r1()));
      break;
    case ignil::Opcode::SGE:
      emit(builder.CreateICmpSGE(r0(), r1()));
      break;

    // --- Control flow ---
    case ignil::Opcode::JMP: {
      auto *target = r0();
      if (const auto *ci = llvm::dyn_cast<llvm::ConstantInt>(target)) {
        auto it = bbMap.find(ci->getZExtValue());
        if (it != bbMap.end()) {
          builder.CreateBr(it->second);
          break;
        }
      }
      builder.CreateUnreachable(); // indirect or out-of-function target
      break;
    }

    case ignil::Opcode::CJMP: {
      auto *cond = r0();
      auto *taken = r1();
      auto *fall = r2();
      const auto *takenCI = llvm::dyn_cast<llvm::ConstantInt>(taken);
      const auto *fallCI = llvm::dyn_cast<llvm::ConstantInt>(fall);
      if (takenCI && fallCI) {
        auto takenIt = bbMap.find(takenCI->getZExtValue());
        auto fallIt = bbMap.find(fallCI->getZExtValue());
        if (takenIt != bbMap.end() && fallIt != bbMap.end()) {
          // Flag regs are i8 (0/1); LLVM br requires i1
          if (cond->getType()->getIntegerBitWidth() != 1)
            cond = builder.CreateTrunc(cond, builder.getInt1Ty());
          builder.CreateCondBr(cond, takenIt->second, fallIt->second);
          break;
        }
      }
      builder.CreateUnreachable();
      break;
    }

    case ignil::Opcode::CALL: {
      auto *ptrTy = llvm::PointerType::getUnqual(*ctx_);
      auto *fnTy = llvm::FunctionType::get(llvm::Type::getVoidTy(*ctx_),
                                           {ptrTy}, false);
      auto *target = r0();
      llvm::Value *callee;
      if (const auto *ci = llvm::dyn_cast<llvm::ConstantInt>(target)) {
        callee =
                module_->getOrInsertFunction(
                               std::format("fn_{:x}", ci->getZExtValue()), fnTy)
                        .getCallee();
      } else {
        callee = target; // indirect call
      }
      builder.CreateCall(fnTy, callee, {memPtr});
      break;
    }

    case ignil::Opcode::RET:
      // Write back the widest register at each offset so callers can observe
      // the final register state even after aggressive optimisation passes.
      for (auto &[offset, widthMap] : regs) {
        auto &[width, slot] = *widthMap.rbegin(); // widest at this offset
        auto *ty  = llvm::IntegerType::get(*ctx_, width);
        auto *val = builder.CreateLoad(ty, slot);
        auto *ptr = builder.CreateGEP(builder.getInt8Ty(), regsPtr,
                                      builder.getInt32(offset));
        builder.CreateStore(val, ptr);
      }
      builder.CreateRetVoid();
      break;

    case ignil::Opcode::UNDEF:
      if (op.dst)
        emit(llvm::UndefValue::get(dstTy()));
      break;
  }
}

llvm::Value *
dragon::llvmlifter::LLVMLifter::resolve(const ignil::Value &v, RegFile &regs,
                                        const TempMap &temps,
                                        llvm::IRBuilder<> &builder) const {

  // Check if it is a constant, in that case, generate
  // a constant llvm value width specific width and value
  if (const auto *c = std::get_if<ignil::Const>(&v)) {
    return llvm::ConstantInt::get(*ctx_, llvm::APInt(c->width, c->value));
  }

  // Check if it is an existing register, in that case
  // we need to create a load
  if (const auto *r = std::get_if<ignil::Reg>(&v)) {
    auto *mem = regs.at(r->offset).at(r->width);
    return builder.CreateLoad(llvm::IntegerType::get(*ctx_, r->width), mem);
  }

  const auto &t = std::get<ignil::Temp>(v);
  return temps.at(t.id);
}
void dragon::llvmlifter::LLVMLifter::mergeIntoParent(
        std::uint32_t offset, ignil::Width width, llvm::Value *value,
        RegFile &regs, llvm::IRBuilder<> &builder) {
  auto it = regs.find(offset);
  if (it == regs.end())
    return;

  for (auto &[bigWidth, slot]: it->second) {
    if (bigWidth <= width)
      continue;

    auto *bigTy = llvm::IntegerType::get(*ctx_, bigWidth);
    auto *mask = llvm::ConstantInt::get(
            bigTy, ~llvm::APInt::getLowBitsSet(bigWidth, width));
    auto *parent = builder.CreateLoad(bigTy, slot);
    auto *cleared = builder.CreateAnd(parent, mask);
    auto *ext = builder.CreateZExt(value, bigTy);
    builder.CreateStore(builder.CreateOr(cleared, ext), slot);
  }
}

// --- Constructor / destructor ---

dragon::llvmlifter::LLVMLifter::LLVMLifter(Architecture arch,
                                           std::string_view moduleName) :
    arch_(arch), ctx_(std::make_unique<llvm::LLVMContext>()),
    module_(std::make_unique<llvm::Module>(moduleName, *ctx_)) {}

dragon::llvmlifter::LLVMLifter::~LLVMLifter() = default;

dragon::llvmlifter::LLVMLifter::LLVMLifter(LLVMLifter &&) noexcept = default;

// --- Optimization passes ---

// Shared boilerplate: run a FunctionPassManager over every non-declaration
// function in the module.
static void runFPM(llvm::Module &mod, llvm::FunctionPassManager fpm) {
  llvm::LoopAnalysisManager lam;
  llvm::FunctionAnalysisManager fam;
  llvm::CGSCCAnalysisManager cgam;
  llvm::ModuleAnalysisManager mam;
  llvm::PassBuilder pb;
  pb.registerModuleAnalyses(mam);
  pb.registerCGSCCAnalyses(cgam);
  pb.registerFunctionAnalyses(fam);
  pb.registerLoopAnalyses(lam);
  pb.crossRegisterProxies(lam, fam, cgam, mam);
  llvm::ModulePassManager mpm;
  mpm.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(fpm)));
  mpm.run(mod, mam);
}

void dragon::llvmlifter::LLVMLifter::runMem2Reg() {
  llvm::FunctionPassManager fpm;
  fpm.addPass(llvm::PromotePass());
  runFPM(*module_, std::move(fpm));
}

void dragon::llvmlifter::LLVMLifter::runInstCombine() {
  llvm::FunctionPassManager fpm;
  fpm.addPass(llvm::InstCombinePass());
  runFPM(*module_, std::move(fpm));
}

void dragon::llvmlifter::LLVMLifter::runGVN() {
  llvm::FunctionPassManager fpm;
  fpm.addPass(llvm::GVNPass());
  runFPM(*module_, std::move(fpm));
}

void dragon::llvmlifter::LLVMLifter::runDCE() {
  llvm::FunctionPassManager fpm;
  fpm.addPass(llvm::DCEPass());
  runFPM(*module_, std::move(fpm));
}

// --- Accessors ---

std::string dragon::llvmlifter::LLVMLifter::ir() const {
  std::string out;
  llvm::raw_string_ostream os(out);
  module_->print(os, nullptr);
  return out;
}

llvm::Module &dragon::llvmlifter::LLVMLifter::module() { return *module_; }
