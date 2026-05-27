#pragma once

#include "LLVMAssemblerTarget.hpp"

#include <llvm/Support/TargetSelect.h>
#include <mutex>

namespace dragon::internal {

/// Initialize LLVM for x86/x86-64 assembly and return a populated target.
/// LLVM registration is performed once via std::call_once — thread safe.
///
/// @param is64bit  true for x86-64, false for i386.
/// @param cpu      LLVM CPU name.  Empty → "generic".
///                 Examples: "skylake", "znver3".
/// @param features LLVM feature string.  Empty → defaults implied by cpu.
///                 Examples: "+avx2,+bmi2".
[[nodiscard]] inline LLVMAssemblerTarget initX86Assembler(
    bool               is64bit,
    const std::string &cpu      = "",
    const std::string &features = "") {
  static std::once_flag flag;
  std::call_once(flag, [] {
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmParser();
    LLVMInitializeX86AsmPrinter();
  });

  const std::string triple =
          is64bit ? "x86_64-unknown-unknown" : "i386-unknown-unknown";

  return buildAssemblerTarget(triple, cpu.empty() ? "generic" : cpu, features);
}

} // namespace dragon::internal
