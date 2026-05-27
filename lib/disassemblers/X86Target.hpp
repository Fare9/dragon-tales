#pragma once

#include "LLVMTarget.hpp"

#include <llvm/Support/TargetSelect.h>
#include <mutex>

namespace dragon::internal {

/// Initialize LLVM for x86/x86-64 and return a populated LLVMTarget.
/// Registration is done once via std::call_once - thread safe.
[[nodiscard]] inline dragon::LLVMTarget initX86(bool is64bit, unsigned syntaxVariant = 0) {
  static std::once_flag flag;
  std::call_once(flag, [is64bit]() {
    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86Disassembler();
    LLVMInitializeX86AsmPrinter();
  });

  const std::string triple =
          is64bit ? "x86_64-unknown-unknown" : "i386-unknown-unknown";

  return buildTarget(triple, "generic", "", syntaxVariant);
}

} // namespace dragon::internal
