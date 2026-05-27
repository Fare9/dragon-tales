#pragma once

#include "LLVMAssemblerTarget.hpp"

#include <llvm/Support/TargetSelect.h>
#include <mutex>

namespace dragon::internal {

/// Initialization function for LLVM objects for RiscV both
/// We only need to call this function once.
[[nodiscard]] inline LLVMAssemblerTarget initRISCVAssembler(bool is64bit) {
  static std::once_flag flag;
  std::call_once(flag, [] {
    LLVMInitializeRISCVTargetInfo();
    LLVMInitializeRISCVTarget();
    LLVMInitializeRISCVTargetMC();
    LLVMInitializeRISCVAsmParser();
    LLVMInitializeRISCVAsmPrinter();
  });

  const std::string triple =
          is64bit ? "riscv64-unknown-unknown" : "riscv32-unknown-unknown";

  return buildAssemblerTarget(triple);
}

} // namespace dragon::internal
