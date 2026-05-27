#pragma once

#include "LLVMAssemblerTarget.hpp"

#include <llvm/Support/TargetSelect.h>
#include <mutex>

namespace dragon::internal {

/// Initialization function for LLVM objects for Intels both
/// We only need to call this function once.
[[nodiscard]] inline LLVMAssemblerTarget initX86Assembler(bool is64bit) {
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

  return buildAssemblerTarget(triple);
}

} // namespace dragon::internal
