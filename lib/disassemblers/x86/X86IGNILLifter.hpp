#pragma once

#include <dragon/Architecture.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>

#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCRegisterInfo.h>

#include "utils/Logger.hpp"

#include <functional>
#include <map>
#include <span>
#include <unordered_map>

namespace dragon {

// Forward declarations, avoid using long headers
class BasicBlock;
class Graph;
class Instruction;

namespace x86 {

/// x86 EFLAGS register identifiers, used as keys in the emitArithFlags map.
enum class X86Flag : std::uint8_t { CF, PF, AF, ZF, SF, OF, DF };

/// Callable that appends IGNIL ops for a single flag into a Block.
/// Receives the block and the running temp-id counter by reference.
using FlagSetter = std::function<void(ignil::Block &, std::uint32_t &)>;

/// A lifter for x86/x86_64 machine instructions to IGNIL
///
/// Construction scans the MCRegisterInfo once to build the
/// LLVM-register integer -> ignil::Reg mapping. Lifting
/// itself is then a pure integer-comparison dispatch with no
/// string work.
/// Not all the instruction set is supported.
class X86IGNILLifter {
  public:
  X86IGNILLifter(internal::Logger& logger, Architecture architecture, const llvm::MCInstrInfo &instrInfo,
                 const llvm::MCRegisterInfo &regInfo);

  /// Lift a complete basic block into an ignil::Block.
  [[nodiscard]] ignil::Block liftBlock(const BasicBlock &block,
                                       const Graph &graph) const;

  /// Lift an arbitrary linear sequence of instructions - e.g. a captured
  /// execution trace, not necessarily a single valid basic block or a
  /// coherent CFG - into one flat ignil::Block by lifting each instruction
  /// independently via the same dispatch liftBlock uses, and concatenating
  /// the results in sequence order.
  ///
  /// Unlike liftBlock, this never consults a Graph and never synthesizes a
  /// fall-through terminator: a trace's control flow is already resolved
  /// by construction (whatever branch each instruction took to reach the
  /// next one in the sequence already happened), so each instruction's own
  /// JMP/CJMP/CALL/RET op - if any - is kept exactly as decoded, targets
  /// and all, rather than second-guessed against block/function structure
  /// this input was never claimed to have.
  [[nodiscard]] ignil::Block
  liftTrace(std::span<const Instruction> trace) const;

  private:
  internal::Logger& logger_;

  Architecture architecture_;
  const llvm::MCInstrInfo *instrInfo_; // used for MCInstrDesc flag checks

  /// Lift a single instruction, appending Ops to out.
  /// tempId is the next available temporary index (incremented as needed).
  void liftInstruction(const Instruction &insn, ignil::Block &out,
                       std::uint32_t &tempId) const;

  // Per-opcode lifting helpers
  void liftMov(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
               std::uint32_t &tmp) const;
  void liftMovLoad(const llvm::MCInst &mc, std::uint64_t addr,
                   ignil::Block &out, std::uint32_t &tmp) const;
  void liftMovStore(const llvm::MCInst &mc, std::uint64_t addr,
                    ignil::Block &out, std::uint32_t &tmp) const;
  void liftAdd(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
               std::uint32_t &tmp) const;
  void liftSub(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
               std::uint32_t &tmp) const;
  // RMW memory-destination forms: op[0..4]=mem(lhs, also destination),
  // op[5]=reg-or-imm(rhs). Loads the current value, computes the result,
  // writes it back to the same address, then sets flags exactly like the
  // register form.
  void liftAddMem(const llvm::MCInst &mc, std::uint64_t addr,
                  ignil::Block &out, std::uint32_t &tmp) const;
  void liftSubMem(const llvm::MCInst &mc, std::uint64_t addr,
                  ignil::Block &out, std::uint32_t &tmp) const;
  // reg += [mem] / reg -= [mem]: op[0]=dst(out), op[1]=src1(tied=dst),
  // op[2..6]=mem(rhs). Loads rhs, then the same ADD/SUB + flags as the rr
  // form. Found by scripts/audit_x86_lifter_coverage.py - the reg,mem
  // direction of the same gap class as liftAddMem/liftSubMem.
  void liftAddRegMem(const llvm::MCInst &mc, std::uint64_t addr,
                     ignil::Block &out, std::uint32_t &tmp) const;
  void liftSubRegMem(const llvm::MCInst &mc, std::uint64_t addr,
                     ignil::Block &out, std::uint32_t &tmp) const;
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
  // CMP reg,mem: op[0]=reg(lhs), op[1..5]=mem(rhs). Loads rhs, discards the
  // SUB result, keeps the flags - same as liftCmp but rhs comes from memory.
  void liftCmpRegMem(const llvm::MCInst &mc, std::uint64_t addr,
                     ignil::Block &out, std::uint32_t &tmp) const;
  // CMP mem,reg / CMP mem,imm: op[0..4]=mem(lhs), op[5]=reg-or-imm(rhs).
  // Loads lhs, discards the SUB result, keeps the flags.
  void liftCmpMem(const llvm::MCInst &mc, std::uint64_t addr,
                  ignil::Block &out, std::uint32_t &tmp) const;
  void liftTest(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
                std::uint32_t &tmp) const;
  void liftJcc(const llvm::MCInst &mc, std::uint64_t addr,
               std::uint8_t insnSize, ignil::Block &out,
               std::uint32_t &tmp) const;
  void liftJmp(const llvm::MCInst &mc, std::uint64_t addr,
               std::uint8_t insnSize, ignil::Block &out,
               std::uint32_t &tmp) const;
  void liftJmpMem(const llvm::MCInst &mc, std::uint64_t addr,
                  std::uint8_t insnSize, ignil::Block &out,
                  std::uint32_t &tmp) const;
  void liftCallMem(const llvm::MCInst &mc, std::uint64_t addr,
                   std::uint8_t insnSize, ignil::Block &out,
                   std::uint32_t &tmp) const;
  void liftCall(const llvm::MCInst &mc, std::uint64_t addr,
                std::uint8_t insnSize, ignil::Block &out,
                std::uint32_t &tmp) const;
  void liftRet(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
               std::uint32_t &tmp) const;
  void liftLeave(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
                 std::uint32_t &tmp) const;
  // Sign-extending move (e.g. `movsxd rax, edi`): op[0]=dst(wider),
  // op[1]=src(narrower). No flags affected. Found while lifting a real
  // compiled signed-division-by-constant sequence (GCC's standard
  // multiply-high lowering for `x / C`), not obfuscation-specific - this
  // shows up in any program that divides by a non-power-of-two constant.
  void liftMovsx(const llvm::MCInst &mc, std::uint64_t addr, ignil::Block &out,
                 std::uint32_t &tmp) const;
  // SAR by immediate: op[0]=dst(out), op[1]=src(tied=dst), op[2]=imm shift
  // count. Real x86: shift by 0 leaves every flag (and the value) untouched
  // - not even recomputed. CF = the last bit shifted out; SF/ZF as usual;
  // OF is only architecturally defined for a 1-bit shift (always 0 for
  // SAR specifically - an arithmetic right shift by exactly one bit can
  // never overflow), left UNDEF otherwise, matching this lifter's existing
  // "UNDEF for genuinely spec-undefined" convention rather than guessing.
  void liftSarImm(const llvm::MCInst &mc, std::uint64_t addr,
                  ignil::Block &out, std::uint32_t &tmp) const;
  // 3-operand signed multiply with immediate (e.g. `imul rax, rax, imm32`):
  // op[0]=dst, op[1]=src, op[2]=imm (already sign-extended to dst's width
  // by LLVM's own MCInst decode, same convention liftAdd already relies on
  // for ADD64ri32). All flags UNDEF: SF/ZF/AF/PF are architecturally
  // undefined for IMUL per the ISA spec, and CF/OF would need a full
  // double-width product to detect overflow correctly, which IGNIL's
  // fixed-width model doesn't support - left honestly UNDEF rather than a
  // guessed, likely-wrong formula.
  void liftImulImm(const llvm::MCInst &mc, std::uint64_t addr,
                   ignil::Block &out, std::uint32_t &tmp) const;
  // LEA computing a 64-bit address expression but writing only the 32-bit
  // low half of the destination (e.g. `lea eax, [rdx+rcx*1]` - address
  // registers are 64-bit, destination is 32-bit). Unlike liftLea, this
  // needs an explicit TRUNC: LLVMLifter's Reg-destination COPY only
  // widens (CreateZExt) when the source is narrower than the slot, and
  // would build invalid IR (or assert) if asked to narrow a 64-bit value
  // into a 32-bit slot directly.
  void liftLeaTrunc32(const llvm::MCInst &mc, std::uint64_t addr,
                      ignil::Block &out, std::uint32_t &tmp) const;

  // Helpers
  // Translate an LLVM register integer to an ignil::Reg.
  // Returns nullopt for unknown registers (caller emits UNDEF).
  [[nodiscard]] std::optional<ignil::Reg> toReg(unsigned llvmReg) const;

  // Allocate a fresh temporary
  [[nodiscard]] ignil::Temp newTemp(std::uint32_t &tempId,
                                    ignil::Width width) const;

  // Decode the 5-operand x86 memory reference starting at
  // mc.getOperand(opStart) (base, scale, index, disp, seg) and emit the
  // arithmetic ops that compute the effective address, returning the Temp that
  // holds the result.
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

  // EFLAGS helpers - iterate every EFLAGS register; for each flag present in
  // flagMap the corresponding FlagSetter is called, otherwise UNDEF is emitted.
  void emitArithFlags(std::uint64_t addr, ignil::Block &out, std::uint32_t &tmp,
                      const std::map<X86Flag, FlagSetter> &flagMap = {}) const;

  // FlagSetter factories - each captures the operands it needs and returns a
  // callable suitable for the flagMap passed to emitArithFlags.

  // ZF = (value == 0)
  [[nodiscard]] FlagSetter zfEqZero(ignil::Value value,
                                    std::uint64_t addr) const;
  // SF = sign bit of value
  [[nodiscard]] FlagSetter sfSignBit(ignil::Value value,
                                     std::uint64_t addr) const;

  // CF = 0 / OF = 0  (logical ops: XOR, AND, OR, TEST always clear both)
  [[nodiscard]] FlagSetter cfAlwaysZero(std::uint64_t addr) const;
  [[nodiscard]] FlagSetter ofAlwaysZero(std::uint64_t addr) const;

  // CF for ADD: unsigned overflow  →  ULT(result, addend)
  [[nodiscard]] FlagSetter cfAddOverflow(ignil::Value result,
                                         ignil::Value addend,
                                         std::uint64_t addr) const;
  // CF for SUB/CMP: unsigned borrow  →  ULT(lhs, rhs)
  [[nodiscard]] FlagSetter cfSubBorrow(ignil::Value lhs, ignil::Value rhs,
                                       std::uint64_t addr) const;

  // OF for ADD: same-sign inputs produce opposite-sign result
  [[nodiscard]] FlagSetter ofAddOverflow(ignil::Value lhs, ignil::Value rhs,
                                         ignil::Value result,
                                         std::uint64_t addr) const;
  // OF for SUB/CMP: different-sign inputs, result sign differs from lhs
  [[nodiscard]] FlagSetter ofSubOverflow(ignil::Value lhs, ignil::Value rhs,
                                         ignil::Value result,
                                         std::uint64_t addr) const;

  // Cached opcode integers (same approach as X86Info)
  unsigned opMov64rr_{}, opMov32rr_{};
  unsigned opMov64ri_{}, opMov32ri_{}, opMov64ri32_{};
  unsigned opMov64rm_{}, opMov32rm_{};
  unsigned opMov64mr_{}, opMov32mr_{};
  unsigned opMov64mi_{}, opMov32mi_{};
  unsigned opLea64r_{}, opLea32r_{};
  unsigned opAdd64rr_{}, opAdd32rr_{};
  unsigned opAdd64ri_{}, opAdd32ri_{}, opAdd64ri8_{}, opAdd32ri8_{};
  unsigned opAdd64mr_{}, opAdd32mr_{};
  unsigned opAdd64mi_{}, opAdd32mi_{}, opAdd64mi8_{}, opAdd32mi8_{};
  unsigned opAdd64rm_{}, opAdd32rm_{};
  unsigned opSub64rr_{}, opSub32rr_{};
  unsigned opSub64ri_{}, opSub32ri_{}, opSub64ri8_{}, opSub32ri8_{};
  unsigned opSub64mr_{}, opSub32mr_{};
  unsigned opSub64mi_{}, opSub32mi_{}, opSub64mi8_{}, opSub32mi8_{};
  unsigned opSub64rm_{}, opSub32rm_{};
  unsigned opXor64rr_{}, opXor32rr_{};
  unsigned opAnd64rr_{}, opAnd32rr_{};
  unsigned opOr64rr_{}, opOr32rr_{};
  unsigned opPush64r_{}, opPop64r_{};
  unsigned opPush32r_{}, opPop32r_{};
  unsigned opCmp64rr_{}, opCmp32rr_{};
  unsigned opCmp64ri_{}, opCmp32ri_{}, opCmp64ri8_{}, opCmp32ri8_{};
  unsigned opCmp64rm_{}, opCmp32rm_{};
  unsigned opCmp64mr_{}, opCmp32mr_{};
  unsigned opCmp64mi_{}, opCmp32mi_{}, opCmp64mi8_{}, opCmp32mi8_{};
  unsigned opTest64rr_{}, opTest32rr_{};
  unsigned opJmp64r_{}, opJmp32r_{}; // indirect
  unsigned opJmp64m_{}, opJmp32m_{}; // indirect memory jump
  unsigned opCall64m_{}, opCall32m_{}; // indirect memory call
  unsigned opJmp_{}; // direct
  unsigned opCall64r_{}; // indirect
  unsigned opCall_{}; // direct
  unsigned opRet64_{}, opRetL_{};
  unsigned opRetI64_{}, opRetIL_{}; // ret N  (callee-cleanup variant)
  unsigned opLeave64_{}, opLeave_{}; // leave (decomposed to mov rsp,rbp / pop rbp)
  unsigned opMovsx64rr32_{};
  unsigned opSar32ri_{}, opSar64ri_{};
  unsigned opImul64rri32_{}, opImul64rri8_{}, opImul32rri_{}, opImul32rri8_{};
  unsigned opLea64_32r_{};

  // LLVM register integer -> ignil::Reg mapping
  // Built once in constructor by scanning MCRegisterInfo +
  // the static name->offset table:
  std::unordered_map<unsigned, ignil::Reg> regMap_;

  // Well-known IGNIL Reg object for flags (generated
  // in the constructor by name).
  ignil::Reg regCF_{}, regPF_{}, regAF_{};
  ignil::Reg regZF_{}, regSF_{}, regOF_{}, regDF_{};
  ignil::Reg regRSP_{}, regESP_{}; // needed for push/pop/leave
  ignil::Reg regRBP_{}, regEBP_{}; // needed for leave decomposition
};

} // namespace x86
} // namespace dragon
