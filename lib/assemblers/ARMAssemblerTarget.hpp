#pragma once

#include "LLVMAssemblerTarget.hpp"

#include <llvm/Support/TargetSelect.h>
#include <mutex>

namespace dragon::internal {

[[nodiscard]] inline LLVMAssemblerTarget initARMAssembler(bool is64bit) {
  static std::once_flag flag;
  std::call_once(flag, [] {
    LLVMInitializeARMTargetInfo();
    LLVMInitializeARMTarget();
    LLVMInitializeARMTargetMC();
    LLVMInitializeARMAsmParser();
    LLVMInitializeARMAsmPrinter();

    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmParser();
    LLVMInitializeAArch64AsmPrinter();
  });

  const std::string triple =
      is64bit ? "aarch64-unknown-unknown" : "arm-unknown-unknown";

  return buildAssemblerTarget(triple);
}

} // namespace dragon::internal
