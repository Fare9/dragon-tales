#pragma once

#include <dragon/Architecture.hpp>
#include <dragon/disassembler/Instruction.hpp>
#include <span>
#include <unordered_set>

#include <llvm/MC/MCInstrInfo.h>


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
  explicit X86Info(Architecture architecture,
                   const llvm::MCInstrInfo &instrInfo,
                   const llvm::MCRegisterInfo &regInfo);

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

  /// Returns true if the instruction is an unconditional software trap
  /// (int3, ud2, ud1, or any opcode flagged as a trap by LLVM).
  [[nodiscard]] bool isTrap(const Instruction &insn) const;

  /// Returns true if the instruction is any NOP variant
  /// (nop, multi-byte nops, fnop).
  [[nodiscard]] bool isNop(const Instruction &insn) const;

  /// Returns true if the instruction is a return from an interrupt handler
  /// (iret, iretd, iretq).
  [[nodiscard]] bool isInterruptReturn(const Instruction &insn) const;

  /// Returns true if the instruction halts the processor (hlt).
  [[nodiscard]] bool isHalt(const Instruction &insn) const;

  private:
  [[nodiscard]] bool
  x8664bitIsCommonPrologue(std::span<const Instruction *> insns) const;
  [[nodiscard]] bool
  x8664bitIsCommonEpilogue(std::span<const Instruction *> insns) const;
  [[nodiscard]] bool
  x8632bitIsCommonPrologue(std::span<const Instruction *> insns) const;
  [[nodiscard]] bool
  x8632bitIsCommonEpilogue(std::span<const Instruction *> insns) const;

  Architecture architecture_;

  // Cached opcode integers (filled in constructor by name scan)
  unsigned opPush64r_{}, opPush32r_{};
  unsigned opMov64rr_{}, opMov32rr_{};
  unsigned opPop64r_{}, opPop32r_{};
  unsigned opLeave64_{}, opLeave_{};
  unsigned opRet64_{}, opRetL_{};
  unsigned opHlt_{};

  // Opcode sets for variable-count instruction families
  std::unordered_set<unsigned> trapOpcodes_; // int3, ud2, ud1, ...
  std::unordered_set<unsigned> nopOpcodes_; // nop, multibyte nops, fnop
  std::unordered_set<unsigned> iretOpcodes_; // iret, iretd, iretq

  // Cached register integers
  unsigned regRBP_{}, regRSP_{};
  unsigned regEBP_{}, regESP_{};
};

} // namespace dragon
