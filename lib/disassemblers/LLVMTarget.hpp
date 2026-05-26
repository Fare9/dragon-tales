#pragma once

#include <memory>
#include <string>

#include <llvm/MC/MCAsmInfo.h>
#include <llvm/MC/MCContext.h>
#include <llvm/MC/MCDisassembler/MCDisassembler.h>
#include <llvm/MC/MCInstPrinter.h>
#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCRegisterInfo.h>
#include <llvm/MC/MCSubtargetInfo.h>

namespace dragon {

struct LLVMTarget {
  std::unique_ptr<llvm::MCRegisterInfo>  regInfo;
  std::unique_ptr<llvm::MCAsmInfo>       asmInfo;
  std::unique_ptr<llvm::MCSubtargetInfo> subtargetInfo;
  std::unique_ptr<llvm::MCInstrInfo>     instrInfo;
  std::unique_ptr<llvm::MCContext>       context;
  std::unique_ptr<llvm::MCDisassembler>  disassembler;
  std::unique_ptr<llvm::MCInstPrinter>   printer;
};

} // namespace dragon

namespace dragon::internal {

[[nodiscard]] LLVMTarget buildTarget(const std::string& triple,
                                     const std::string& cpu      = "generic",
                                     const std::string& features = "");

} // namespace dragon::internal
