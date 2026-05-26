#include "LLVMTarget.hpp"

#include <llvm/MC/MCTargetOptions.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/TargetParser/Triple.h>

dragon::LLVMTarget dragon::internal::buildTarget(const std::string &triple,
                                                 const std::string &cpu,
                                                 const std::string &features) {
  dragon::LLVMTarget target;

  // 1. First we look for the target from the LLVM registry
  // If the arch was not registered, this will fail and we return
  // an empty target
  std::string error;
  const llvm::Target *llvmTarget =
          llvm::TargetRegistry::lookupTarget(triple, error);
  if (!llvmTarget)
    return target;

  // 2. Register info - describes registers for this arch
  target.regInfo.reset(llvmTarget->createMCRegInfo(triple));
  if (!target.regInfo)
    return target;

  // 3. Asm info - syntax, directives, comment characters
  llvm::MCTargetOptions targetOptions;
  target.asmInfo.reset(
          llvmTarget->createMCAsmInfo(*target.regInfo, triple, targetOptions));
  if (!target.asmInfo)
    return target;

  // 4. Instruction info - opcode descriptions, flags (isCall, isBranch...)
  target.instrInfo.reset(llvmTarget->createMCInstrInfo());
  if (!target.instrInfo)
    return target;

  // 5. Subtarget info - CPU features, scheduling model
  target.subtargetInfo.reset(
          llvmTarget->createMCSubtargetInfo(triple, cpu, features));
  if (!target.subtargetInfo)
    return target;

  // 6. Context — ties everything together, needed for disassembler
  target.context = std::make_unique<llvm::MCContext>(
          llvm::Triple(triple), target.asmInfo.get(), target.regInfo.get(),
          target.subtargetInfo.get());

  // 7. Disassembler — the actual decoder
  target.disassembler.reset(llvmTarget->createMCDisassembler(
          *target.subtargetInfo, *target.context));
  if (!target.disassembler)
    return target;

  // 8. Printer — converts MCInst to mnemonic + operands strings
  // 0 = default syntax variant for the arch
  target.printer.reset(llvmTarget->createMCInstPrinter(
          llvm::Triple(triple), 0, *target.asmInfo, *target.instrInfo,
          *target.regInfo));

  return target;
}
