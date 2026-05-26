#pragma once

#include <memory>
#include <string>

#include <llvm/MC/MCAsmInfo.h>
#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCRegisterInfo.h>
#include <llvm/MC/MCSubtargetInfo.h>

namespace dragon {

struct LLVMAssemblerTarget {
  std::unique_ptr<llvm::MCRegisterInfo>  regInfo;
  std::unique_ptr<llvm::MCAsmInfo>       asmInfo;
  std::unique_ptr<llvm::MCSubtargetInfo> subtargetInfo;
  std::unique_ptr<llvm::MCInstrInfo>     instrInfo;
  std::string triple;
  // NOTE: context, backend, and emitter are recreated per assemble() call
  //       because they are stateful and consumed by the streamer
};

} // namespace dragon

namespace dragon::internal {

[[nodiscard]] LLVMAssemblerTarget buildAssemblerTarget(
    const std::string& triple,
    const std::string& cpu      = "generic",
    const std::string& features = "");

} // namespace dragon::internal
