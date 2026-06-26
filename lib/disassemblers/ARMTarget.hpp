#pragma once

#include "LLVMTarget.hpp"

#include <llvm/Support/TargetSelect.h>
#include <mutex>

namespace dragon::internal {

/// Initialize LLVM for ARM/AArch64 and return a populated LLVMTarget.
/// Registration is done once via std::call_once - thread safe.
///
/// @param is64bit  true for AArch64, false for ARM32.
/// @param cpu      LLVM CPU name.  Empty → "generic".
///                 ARM32 examples: "arm926ej-s" (ARMv5TEJ, enables BX/BLX),
///                                 "cortex-a8"  (ARMv7-A, Thumb2, NEON).
///                 ARM64 examples: "cortex-a53", "apple-m1".
/// @param features LLVM feature string.  Empty → defaults implied by cpu.
///                 ARM32 examples: "+v4t" (adds BX), "+v7,+neon".
///                 ARM64 examples: "+sve", "+crypto".
[[nodiscard]] inline LLVMTarget initARM(
    bool               is64bit,
    const std::string &cpu      = "",
    const std::string &features = "") {
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

  return buildTarget(triple, cpu.empty() ? "generic" : cpu, features);
}

} // namespace dragon::internal