#pragma once

#include <dragon/Architecture.hpp>
#include <dragon/Instruction.hpp>
#include <span>

namespace dragon {

/// Instruction classification utilities for the x86 and x86_64 architectures.
///
/// X86Info operates on already decoded Instruction objects, using the mnemonic
/// and operand strings produced by the disassembler. It does not depend on LLVM
/// headers or on the Disassembler class and can be used independently by
/// library users who want to query instruction properties directly.
///
/// Mode dependent methods (prologue and epilogue detection) consult the
/// Architecture value supplied at construction time. All other methods are
/// stateless and are exposed as static members.
class X86Info {
  public:
  /// Constructs an X86Info for the given architecture.
  /// The architecture must be Architecture::X86_32 or Architecture::X86_64.
  explicit X86Info(Architecture architecture);

  /// Returns true if the instruction sequence matches a common x86 function
  /// prologue for the architecture this object was constructed with.
  ///
  /// At least two instructions must be provided. Returns false if insns is
  /// shorter than the minimum required window.
  ///
  /// x86_64: push rbp followed by mov rbp, rsp
  /// x86_32: push ebp followed by mov ebp, esp
  [[nodiscard]] bool
  isCommonFunctionPrologue(std::span<const Instruction *> insns) const;

  /// Returns true if the instruction sequence matches a common x86 function
  /// epilogue for the architecture this object was constructed with.
  ///
  /// At least two instructions must be provided. Returns false if insns is
  /// shorter than the minimum required window.
  ///
  /// Recognised patterns for both modes:
  ///   pop rbp/ebp followed by ret
  ///   leave followed by ret
  [[nodiscard]] bool
  isCommonFunctionEpilogue(std::span<const Instruction *> insns) const;

  /// Returns true if the instruction is an unconditional software trap.
  /// Recognised mnemonics: int3, ud2, ud1.
  [[nodiscard]] static bool isTrap(const Instruction &insn);

  /// Returns true if the instruction is any NOP variant.
  /// Recognised mnemonics: nop (including multi byte forms), fnop.
  [[nodiscard]] static bool isNop(const Instruction &insn);

  /// Returns true if the instruction is a return from an interrupt handler.
  /// Recognised mnemonics: iret, iretd, iretq.
  [[nodiscard]] static bool isInterruptReturn(const Instruction &insn);

  /// Returns true if the instruction halts the processor.
  /// Recognised mnemonic: hlt.
  [[nodiscard]] static bool isHalt(const Instruction &insn);

  private:
  Architecture architecture_;
};

} // namespace dragon
