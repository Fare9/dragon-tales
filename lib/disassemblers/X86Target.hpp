#pragma once

#include "LLVMTarget.hpp"

#include <llvm/Support/TargetSelect.h>
#include <mutex>

namespace dragon::internal {

/// Initialize LLVM for x86/x86-64 and return a populated LLVMTarget.
/// Registration is done once via std::call_once — thread safe.
///
/// @param is64bit       true for x86-64, false for i386.
/// @param syntaxVariant 0 = AT&T, 1 = Intel (x86 only).
/// @param cpu           LLVM CPU name (e.g. "skylake").  Empty → "generic".
/// @param features      LLVM feature string (e.g. "+avx2").  Empty → defaults.
[[nodiscard]] inline dragon::LLVMTarget initX86(
    bool               is64bit,
    unsigned           syntaxVariant = 0,
    const std::string &cpu           = "",
    const std::string &features      = "") {
  static std::once_flag flag;
  std::call_once(flag, [] {
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86Disassembler();
    LLVMInitializeX86AsmPrinter();
  });

  const std::string triple =
          is64bit ? "x86_64-unknown-unknown" : "i386-unknown-unknown";

  return buildTarget(triple, cpu.empty() ? "generic" : cpu, features, syntaxVariant);
}

} // namespace dragon::internal
