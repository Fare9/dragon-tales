#pragma once

#include "LLVMTarget.hpp"

#include <llvm/Support/TargetSelect.h>
#include <mutex>

namespace dragon::internal {

/// Initialize LLVM for RISC-V 32/64 and return a populated LLVMTarget.
/// Registration is done once via std::call_once - thread safe.
///
/// @param is64bit  true for RV64, false for RV32.
/// @param cpu      LLVM CPU name.  Empty → "generic".
///                 Examples: "sifive-u74", "rocket".
/// @param features LLVM feature string.  Empty → defaults implied by cpu.
///                 Examples: "+m,+a,+c" (multiply, atomics, compressed),
///                            "+f,+d"   (single- and double-precision FP).
[[nodiscard]] inline dragon::LLVMTarget initRISCV(
    bool               is64bit,
    const std::string &cpu      = "",
    const std::string &features = "") {
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

  return buildTarget(triple, cpu.empty() ? "generic" : cpu, features);
}

} // namespace dragon::internal