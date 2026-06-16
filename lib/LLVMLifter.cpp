#include <dragon/lifters/llvm/LLVMLifter.hpp>

#include <utility>


dragon::llvmlifter::LLVMLifter::RegFile
dragon::llvmlifter::LLVMLifter::buildRegFile(llvm::Function *llfn,
                                             const Function *fn,
                                             const Graph &graph) {
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
std::map<std::uint64_t, llvm::BasicBlock *>
dragon::llvmlifter::LLVMLifter::buildBlockMap(llvm::Function *llfn,
                                              const Function *fn,
                                              const Graph &graph) {
  std::map<std::uint64_t, llvm::BasicBlock *> addrBlock;

  for ()
}
