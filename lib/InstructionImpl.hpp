#pragma once

/// Internal header - do NOT include from public headers or the C shim.
/// Include from implementation (.cpp) files that need direct access to the
/// underlying LLVM MCInst (e.g. Disassembler.cpp, future analysis passes).

#include <llvm/MC/MCInst.h>

#include <dragon/Instruction.hpp>

namespace dragon {

/// Full definition of Instruction::Impl.
/// Kept out of the public Instruction.hpp so that LLVM headers are never
/// transitively pulled into user code.
struct Instruction::Impl {
  /// The raw LLVM machine-code instruction produced by the disassembler.
  /// Provides access to the opcode, operands, and any LLVM-level metadata
  /// that is not surfaced through the dragon::Instruction public API.
  llvm::MCInst inst;
};

} // namespace dragon
