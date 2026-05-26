#pragma once

#include "LLVMTarget.hpp"

#include <llvm/Support/TargetSelect.h>
#include <mutex>

namespace dragon::internal {

/// Initialize LLVM for ARM/ARM64 and return a populated LLVMTarget.
/// Registration is done once via std::call_once — thread safe.
[[nodiscard]] inline dragon::LLVMTarget initARM(bool is64bit) {
  static std::once_flag flag;
  std::call_once(flag, [] {
      LLVMInitializeARMTargetInfo();
      LLVMInitializeARMTarget();
      LLVMInitializeARMTargetMC();
      LLVMInitializeARMDisassembler();
      LLVMInitializeARMAsmPrinter();

      LLVMInitializeAArch64TargetInfo();
      LLVMInitializeAArch64Target();
      LLVMInitializeAArch64TargetMC();
      LLVMInitializeAArch64Disassembler();
      LLVMInitializeAArch64AsmPrinter();
  });

  const std::string triple = is64bit
      ? "aarch64-unknown-unknown"
      : "arm-unknown-unknown";

  return buildTarget(triple);
}

} // namespace dragon::internal