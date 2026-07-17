#pragma once

#include <dragon/Architecture.hpp>
#include <dragon/disassembler/Instruction.hpp>
#include <span>
#include <unordered_set>

#include <llvm/MC/MCInstrInfo.h>


namespace dragon {

/// Instruction classification utilities for armv32 and arm64 architectures.
///
/// ARMInfo operates on already decoded Instruction objects, using the mnemonic
/// and operand strings produced by the disassembler. It does not depend on LLVM
/// headers or on the Disassembler class and can be used independently by
/// library users who want to query instruction properties directly.
///
/// Mode dependent methods (prologue and epilogue detection) consult the
/// Architecture value supplied at construction time. All other methods are
/// stateless and are exposed as static members.
class ARMInfo {
  public:
  explicit ARMInfo(Architecture architecture,
                   const llvm::MCInstrInfo &instrInfo,
                   const llvm::MCRegisterInfo &regInfo);

  /// Returns the number of instructions consumed if the sequence matches a
  /// common ARM function prologue, or 0 if no match.
  ///
  /// AArch64:
  ///   paciasp / bti c                          -> 1  (landing pad alone)
  ///   stp x29, x30, [sp, #-N]!                -> 1  (frameless save)
  ///   stp x29, x30, [sp, #-N]! / mov x29, sp  -> 2  (full frame)
  ///   paciasp + above                          -> 2 or 3
  ///   Optionally extended by a leading "sub sp, sp, #N" ahead of an
  ///   offset-form stp (large frame), and/or a trailing "sub sp, sp, #N"
  ///   after "mov x29, sp" (locals beyond the frame record).
  ///
  /// ARM32:
  ///   push {rX-rY, lr}                         -> 1
  ///   push {rX-rY, lr} / add r11, sp, #N       -> 2
  ///   Frame pointer setup also accepts r7 (Thumb/APCS convention), and
  ///   may be followed by an optional "sub sp, sp, #N" for locals.
  [[nodiscard]] std::size_t
  isCommonFunctionPrologue(std::span<const Instruction *> insns) const;

  /// Returns the number of instructions consumed if the sequence matches a
  /// common ARM function epilogue, or 0 if no match.
  ///
  /// AArch64:
  ///   ret                                      -> 1
  ///   ldp x29, x30, [sp], #N / ret             -> 2
  ///   autiasp / ldp x29, x30, [sp], #N / ret   -> 3
  ///
  /// ARM32:
  ///   pop {rX-rY, pc}                          -> 1
  ///   bx lr                                    -> 1
  ///   pop {rX-rY, lr} / bx lr                  -> 2
  [[nodiscard]] std::size_t
  isCommonFunctionEpilogue(std::span<const Instruction *> insns) const;

  [[nodiscard]] bool isTrap(const Instruction &insn) const;
  [[nodiscard]] bool isNop(const Instruction &insn) const;

  private:
  [[nodiscard]] std::size_t
  arm64IsCommonPrologue(std::span<const Instruction *> insns) const;
  [[nodiscard]] std::size_t
  arm64IsCommonEpilogue(std::span<const Instruction *> insns) const;
  [[nodiscard]] std::size_t
  arm32IsCommonPrologue(std::span<const Instruction *> insns) const;
  [[nodiscard]] std::size_t
  arm32IsCommonEpilogue(std::span<const Instruction *> insns) const;


  Architecture architecture_;

  // -- AArch64 opcodes -------------------------------------------------------
  unsigned opSTPXpre_{}; // stp x29, x30, [sp, #-N]!
  unsigned opSTPXi_{}; // stp x29, x30, [sp, #M]
  unsigned opLDPXpost_{}; // ldp x29, x30, [sp], #N
  unsigned opLDPXi_{}; // ldp x29, x30, [sp, #M]
  unsigned opSUBXri_{}; // sub sp, sp, #N
  unsigned opADDXri_{}; // add x29, sp, #M
  unsigned opRET_{}; // ret
  unsigned opPACIASP_{}; // paciasp
  unsigned opAUTIASP_{}; // autiasp
  unsigned opBTI_{}; // bti
  unsigned opHINT_{}; // hint #imm (nop is hint #0) - AArch64 and ARM32 (A32)

  // -- ARM32 / Thumb opcodes -------------------------------------------------
  unsigned opPUSH_{}; // push {reglist} (ARM)
  unsigned opPOP_{}; // pop {reglist} (ARM)
  unsigned opSTMDB_{}; // stmdb sp!, {reglist}
  unsigned opLDMIA_{}; // ldmia sp!, {reglist}
  unsigned opBXRET_{}; // bx lr (RET)
  unsigned opMOVPCLR_{}; // mov pc, lr (old-style return)
  unsigned opTPUSH_{}; // tpush (Thumb)
  unsigned opTPOP_{}; // tpop (Thumb)
  unsigned opTPOPRET_{}; // tpop with PC (Thumb combined restore+return)
  unsigned opTBXRET_{}; // tbx lr (Thumb)
  unsigned opSUBri_{}; // sub sp, sp, #N (ARM32)
  unsigned opADDri_{}; // add rX, sp, #N (ARM32 frame pointer setup)
  unsigned opTHINT_{}; // hint #imm (Thumb)

  // -- Register IDs ----------------------------------------------------------
  unsigned regX29_{}, regX30_{}, regSP64_{}; // AArch64
  unsigned regR7_{}, regR11_{}, regLR_{}, regPC_{}, regSP32_{}; // ARM32

  std::unordered_set<unsigned> trapOpcodes_;
};

} // namespace dragon
