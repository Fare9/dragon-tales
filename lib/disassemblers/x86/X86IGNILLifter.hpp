#pragma once

#include <dragon/Architecture.hpp>
#include <dragon/IGNIL.hpp>
#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCRegisterInfo.h>

#include <unordered_map>

namespace dragon {

// Forward declarations, avoid using long headers
class BasicBlock;
class Graph;
class Instruction;

namespace x86 {

/// A lifter for x86/x86_64 machine instructions to IGNIL
///
/// Construction scans the MCRegisterInfo once to build the
/// LLVM-register integer -> ignil::Reg mapping. Lifting
/// itself is then a pure integer-comparison dispatch with no
/// string work.
/// Not all the instruction set is supported.
class X86IGNILLifter {
  public:
  X86IGNILLifter(Architecture architecture, const llvm::MCInstrInfo &instrInfo,
                 const llvm::MCRegisterInfo &regInfo);

  /// Lift a complete basic block into an ignil::Block.
  [[nodiscard]] ignil::Block liftBlock(const BasicBlock &block,
                                       const Graph &graph) const;

  private:

  Architecture architecture_;
  const llvm::MCInstrInfo *instrInfo_; // used for MCInstrDesc flag checks

  /// Lift a single instruction, appending Ops to out.
  /// tempId is the next available temporary index (incremented as needed).
  void liftInstruction(const Instruction &insn, ignil::Block &out,
                       std::uint32_t &tempId) const;

  // Per-opcode lifting helpers
  void liftMov(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
               std::uint32_t &tmp) const;
  void liftAdd(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
               std::uint32_t &tmp) const;
  void liftSub(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
               std::uint32_t &tmp) const;
  void liftXor(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
               std::uint32_t &tmp) const;
  void liftAnd(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
               std::uint32_t &tmp) const;
  void liftOr(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
              std::uint32_t &tmp) const;
  void liftPush(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
                std::uint32_t &tmp) const;
  void liftPop(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
               std::uint32_t &tmp) const;
  void liftLea(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
               std::uint32_t &tmp) const;
  void liftCmp(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
               std::uint32_t &tmp) const;
  void liftTest(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
                std::uint32_t &tmp) const;
  void liftJcc(const llvm::MCInst &mc, std::uint64_t addr,
               std::uint8_t insnSize, ignil::Block &out,
               std::uint32_t &tmp) const;
  void liftJmp(const llvm::MCInst &mc, std::uint64_t addr,
               std::uint8_t insnSize, ignil::Block &out,
               std::uint32_t &tmp) const;
  void liftCall(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
                std::uint32_t &tmp) const;
  void liftRet(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
               std::uint32_t &tmp) const;

  // Helpers
  // Translate an LLVM register integer to an ignil::Reg.
  // Returns nullopt for unknown registers (caller emits UNDEF).
  [[nodiscard]] std::optional<ignil::Reg> toReg(unsigned llvmReg) const;

  // Allocate a fresh temporary
  [[nodiscard]] ignil::Temp newTemp(std::uint32_t &tempId,
                                    ignil::Width width) const;

  // Decode the 5-operand x86 memory reference starting at mc.getOperand(opStart)
  // (base, scale, index, disp, seg) and emit the arithmetic ops that compute
  // the effective address, returning the Temp that holds the result.
  [[nodiscard]] ignil::Temp emitAddress(const llvm::MCInst &mc,
                                        unsigned opStart, ignil::Width ptrWidth,
                                        std::uint64_t addr, ignil::Block &out,
                                        std::uint32_t &tmp) const;

  // Emit ops that compute the 1-bit boolean condition for a Jcc from the
  // current flag registers. condCode is the LLVM x86 CondCode immediate
  // (0-15) found in op[1] of the unified JCC_1/JCC_4 MCInst.
  [[nodiscard]] ignil::Temp emitCondition(unsigned condCode, std::uint64_t addr,
                                          ignil::Block &out,
                                          std::uint32_t &tmp) const;

  // Emit UNDEF for a destination register (conservative for
  // instructions we do not yet lift).
  void emitUndef(ignil::Reg dst, std::uint64_t addr, ignil::Block &out) const;

  // EFLAGS helpers - emit standard arithmetic flag updates
  // after an operation whose result is in `result`.
  void emitArithFlags(ignil::Value result, std::uint64_t addr,
                      ignil::Block &out, std::uint32_t &tmp) const;

  // Cached opcode integers (same approach as X86Info)
  unsigned opMov64rr_{}, opMov32rr_{};
  unsigned opMov64ri_{}, opMov32ri_{};
  unsigned opMov64rm_{}, opMov32rm_{};
  unsigned opMov64mr_{}, opMov32mr_{};
  unsigned opLea64r_{}, opLea32r_{};
  unsigned opAdd64rr_{}, opAdd32rr_{};
  unsigned opAdd64ri_{}, opAdd32ri_{};
  unsigned opSub64rr_{}, opSub32rr_{};
  unsigned opSub64ri_{}, opSub32ri_{};
  unsigned opXor64rr_{}, opXor32rr_{};
  unsigned opAnd64rr_{}, opAnd32rr_{};
  unsigned opOr64rr_{}, opOr32rr_{};
  unsigned opPush64r_{}, opPop64r_{};
  unsigned opPush32r_{}, opPop32r_{};
  unsigned opCmp64rr_{}, opCmp32rr_{};
  unsigned opCmp64ri_{}, opCmp32ri_{};
  unsigned opTest64rr_{}, opTest32rr_{};
  unsigned opJmp64r_{}, opJmp32r_{}; // indirect
  unsigned opJmp_{}; // direct
  unsigned opCall64r_{}; // indirect
  unsigned opCall_{}; // direct
  unsigned opRet64_{}, opRetL_{};

  // LLVM register integer -> ignil::Reg mapping
  // Built once in constructor by scanning MCRegisterInfo +
  // the static name->offset table:
  std::unordered_map<unsigned, ignil::Reg> regMap_;

  // Well-known IGNIL Reg object for flags (generated
  // in the constructor by name).
  ignil::Reg regCF_{}, regPF_{}, regAF_{};
  ignil::Reg regZF_{}, regSF_{}, regOF_{}, regDF_{};
  ignil::Reg regRSP_{}, regESP_{}; // needed for push/pop
};

} // namespace x86
} // namespace dragon
