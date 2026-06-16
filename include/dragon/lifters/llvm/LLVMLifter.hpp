#pragma once

#include <dragon/Architecture.hpp>
#include <dragon/disassembler/Function.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <map>
#include <memory>
#include <string_view>

namespace dragon::llvmlifter {

class LLVMLifter {
  public:
  explicit LLVMLifter(Architecture arch,
                      std::string_view moduleName = "lifted");
  ~LLVMLifter();

  LLVMLifter(const LLVMLifter &) = delete;
  LLVMLifter &operator=(const LLVMLifter &) = delete;
  LLVMLifter(LLVMLifter &&) noexcept;
  LLVMLifter &operator=(LLVMLifter &&) noexcept = default;

  /// Lift one function (all its IGNIL blocks must be in the graph).
  bool lift(const Function &fn, const Graph &graph);

  // Run optimization passes in the recommended order.
  void runMem2Reg(); // promotes allocas → SSA, inserts PHIs
  void runInstCombine(); // folds sub-register mask/OR patterns
  void runGVN(); // eliminates redundant loads
  void runDCE(); // removes dead flag computations

  [[nodiscard]] std::string ir() const; // dump textual LLVM IR
  [[nodiscard]] llvm::Module &module();

  private:
  // All allocas for a lifted function.
  // In this way we keep the registers that have been
  // used, and we only generate those that are used by
  // the function.
  // map<register offset, map<bit width, AllocaInst>>
  using RegFile =
          std::map<std::uint32_t, std::map<ignil::Width, llvm::AllocaInst *>>;

  // SSA values produced by Temp operands within the current block.
  using TempMap = std::map<std::uint32_t, llvm::Value *>;

  // First pass: scan every IGNIL op in every block of the function,
  // collect all (offset, width) pairs, emit one alloca per pair
  // in the entry block.
  RegFile buildRegFile(llvm::Function *llfn, const Function *fn,
                       const Graph &graph) const;

  // Map each IGNIL block address to a llvm::BasicBlock.
  // Returns nullopt if any block in fn is missing an IGNIL translation,
  // since the function cannot be correctly lifted with a gap.
  std::optional<std::map<std::uint64_t, llvm::BasicBlock *>>
  buildBlockMap(llvm::Function *llfn, const Function *fn,
                const Graph &graph) const;

  // Translate one IGNIL block into its llvm::BasicBlock.
  void liftBlock(const ignil::Block *ib, llvm::Function *llfn, RegFile *regs,
                 TempMap *temps, llvm::Value *memPtr, llvm::Value *regsPtr,
                 const std::map<std::uint64_t, llvm::BasicBlock *> &bbMap);

  // Translate one IGNIL Op; records the result in temps if the op has a
  // Temp destination
  void liftOp(const ignil::Op &op, RegFile &regs, TempMap &temps,
              llvm::Value *memPtr, llvm::Value *regsPtr,
              llvm::IRBuilder<> &builder,
              const std::map<std::uint64_t, llvm::BasicBlock *> &bbMap);

  // Resolve an ignil::Value to a llvm::Value (load from alloca or
  // look up the temp SSA value).
  llvm::Value *resolve(const ignil::Value &v, RegFile &regs,
                       const TempMap &temps, llvm::IRBuilder<> &builder) const;

  // After writing `value` (of `width` bits) to `offset`, propagate
  // into every wider alloca that exists at the same offset.
  void mergeIntoParent(std::uint32_t offset, ignil::Width width,
                       llvm::Value *value, RegFile &regs,
                       llvm::IRBuilder<> &builder);

  Architecture arch_;
  std::unique_ptr<llvm::LLVMContext> ctx_;
  std::unique_ptr<llvm::Module> module_;
};

} // namespace dragon::llvmlifter
