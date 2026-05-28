#include "assemblers/LLVMAssemblerTarget.hpp"

#include <llvm/MC/MCTargetOptions.h>
#include <llvm/MC/TargetRegistry.h>

dragon::LLVMAssemblerTarget
dragon::internal::buildAssemblerTarget(const std::string& triple,
                                        const std::string& cpu,
                                        const std::string& features) {
  LLVMAssemblerTarget target;

  std::string error;
  const llvm::Target* llvmTarget =
      llvm::TargetRegistry::lookupTarget(triple, error);
  if (!llvmTarget)
    return target;

  target.regInfo.reset(llvmTarget->createMCRegInfo(triple));
  if (!target.regInfo)
    return target;

  llvm::MCTargetOptions targetOptions;
  target.asmInfo.reset(
      llvmTarget->createMCAsmInfo(*target.regInfo, triple, targetOptions));
  if (!target.asmInfo)
    return target;

  target.instrInfo.reset(llvmTarget->createMCInstrInfo());
  if (!target.instrInfo)
    return target;

  target.subtargetInfo.reset(
      llvmTarget->createMCSubtargetInfo(triple, cpu, features));
  if (!target.subtargetInfo)
    return target;

  target.triple = triple;

  return target;
}
