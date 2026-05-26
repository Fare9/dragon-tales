#pragma once

#include "LLVMTarget.hpp"

#include <llvm/Support/TargetSelect.h>
#include <mutex>

namespace dragon::internal {

/// Initialize LLVM for RISC-V 32/64 and return a populated LLVMTarget.
/// Registration is done once via std::call_once — thread safe.
[[nodiscard]] inline dragon::LLVMTarget initRISCV(bool is64bit) {
  static std::once_flag flag;
  std::call_once(flag, [] {
      LLVMInitializeRISCVTargetInfo();
      LLVMInitializeRISCVTarget();
      LLVMInitializeRISCVTargetMC();
      LLVMInitializeRISCVDisassembler();
      LLVMInitializeRISCVAsmPrinter();
  });

  const std::string triple = is64bit
      ? "riscv64-unknown-unknown"
      : "riscv32-unknown-unknown";

  return buildTarget(triple);
}

} // namespace dragon::internal