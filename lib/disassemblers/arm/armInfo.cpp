#include <dragon/disassembler/arm/armInfo.hpp>

#include <llvm/MC/MCInst.h>
#include <llvm/MC/MCInstrDesc.h>
#include <llvm/MC/MCRegisterInfo.h>

#include "InstructionImpl.hpp"

namespace {
bool mcHasReg(const llvm::MCInst &mc, unsigned reg) {
  for (unsigned i = 0; i < mc.getNumOperands(); ++i)
    if (mc.getOperand(i).isReg() && mc.getOperand(i).getReg() == reg)
      return true;
  return false;
}
} // namespace

dragon::ARMInfo::ARMInfo(Architecture arch, const llvm::MCInstrInfo &instrInfo,
                         const llvm::MCRegisterInfo &regInfo) :
    architecture_(arch) {
  for (unsigned i = 0; i < instrInfo.getNumOpcodes(); ++i) {
    const std::string_view name = instrInfo.getName(i);

    if (instrInfo.get(i).isTrap()) {
      trapOpcodes_.insert(i);
      continue; // we already know it's a trap, continue
    }

    // AArch64
    if (name == "STPXpre")
      opSTPXpre_ = i;
    else if (name == "STPXi")
      opSTPXi_ = i;
    else if (name == "LDPXpost")
      opLDPXpost_ = i;
    else if (name == "LDPXi")
      opLDPXi_ = i;
    else if (name == "SUBXri")
      opSUBXri_ = i;
    else if (name == "ADDXri")
      opADDXri_ = i;
    else if (name == "RET_ReallyLR")
      opRET_ = i;
    else if (name == "PACIASP")
      opPACIASP_ = i;
    else if (name == "AUTIASP")
      opAUTIASP_ = i;
    else if (name == "BTI")
      opBTI_ = i;
    // ARM32 / Thumb
    else if (name == "PUSH")
      opPUSH_ = i;
    else if (name == "POP")
      opPOP_ = i;
    else if (name == "STMDB_UPD")
      opSTMDB_ = i;
    else if (name == "LDMIA_UPD")
      opLDMIA_ = i;
    else if (name == "BX_RET")
      opBXRET_ = i;
    else if (name == "MOVPCLR")
      opMOVPCLR_ = i;
    else if (name == "tPUSH")
      opTPUSH_ = i;
    else if (name == "tPOP")
      opTPOP_ = i;
    else if (name == "tPOP_RET")
      opTPOPRET_ = i;
    else if (name == "tBX_RET")
      opTBXRET_ = i;
    else if (name == "SUBri")
      opSUBri_ = i;
    else if (name == "ADDri")
      opADDri_ = i;
    // NOPs — both architectures
    else if (name == "NOP" || name == "tNOP" || name.starts_with("NOP"))
      nopOpcodes_.insert(i);
  }

  for (unsigned i = 0; i < regInfo.getNumRegs(); ++i) {
    const std::string_view name = regInfo.getName(i);
    if (name == "X29")
      regX29_ = i;
    else if (name == "X30")
      regX30_ = i;
    else if (name == "SP") {
      regSP64_ = i;
      regSP32_ = i;
    } else if (name == "R7")
      regR7_ = i;
    else if (name == "R11")
      regR11_ = i;
    else if (name == "LR")
      regLR_ = i;
    else if (name == "PC")
      regPC_ = i;
  }
}

std::size_t dragon::ARMInfo::isCommonFunctionPrologue(
        std::span<const Instruction *> insns) const {
  if (architecture_ == Architecture::ARM64) {
    return arm64IsCommonPrologue(insns);
  }
  if (architecture_ == Architecture::ARM32) {
    return arm32IsCommonPrologue(insns);
  }
  return 0;
}

std::size_t dragon::ARMInfo::isCommonFunctionEpilogue(
        std::span<const Instruction *> insns) const {
  if (architecture_ == Architecture::ARM64) {
    return arm64IsCommonEpilogue(insns);
  }
  if (architecture_ == Architecture::ARM32) {
    return arm32IsCommonEpilogue(insns);
  }
  return 0;
}

std::size_t dragon::ARMInfo::arm64IsCommonPrologue(
        std::span<const Instruction *> insns) const {
  if (insns.empty())
    return 0;

  std::size_t pos = 0;

  // Optional: paciasp or bti c (landing pad / PAC prefix)
  if (const auto *mc = insns[0]->mcInst(); mc) {
    const unsigned op = mc->getOpcode();
    if (op == opPACIASP_ || op == opBTI_)
      pos = 1;
  }

  if (pos == insns.size())
    return pos; // landing pad alone is a valid entry point

  // stp x29, x30, [sp, #-N]! (STPXpre)
  // or stp x29, x30, [sp, #M] (STPXi, less common in prologues)
  const auto *stpMC = insns[pos]->mcInst();
  if (!stpMC)
    return pos;

  const unsigned opStp = stpMC->getOpcode();
  if ((opStp != opSTPXpre_ && opStp != opSTPXi_) ||
      !mcHasReg(*stpMC, regX29_) || !mcHasReg(*stpMC, regX30_) ||
      !mcHasReg(*stpMC, regSP64_))
    return pos; // no STP match — return landing-pad count (0 or 1)

  pos += 1;

  // Optional: add x29, sp, #0  ("mov x29, sp" — sets up frame pointer)
  // This is ADDXri with dst=X29, src=SP, imm=0
  if (pos < insns.size()) {
    const auto *addMC = insns[pos]->mcInst();
    if (addMC && addMC->getOpcode() == opADDXri_ &&
        addMC->getNumOperands() >= 2 && addMC->getOperand(0).isReg() &&
        addMC->getOperand(0).getReg() == regX29_ &&
        addMC->getOperand(1).isReg() &&
        addMC->getOperand(1).getReg() == regSP64_)
      pos += 1;
  }

  return pos;
}

std::size_t dragon::ARMInfo::arm64IsCommonEpilogue(
        std::span<const Instruction *> insns) const {
  if (insns.empty())
    return 0;

  const auto *i0 = insns[0]->mcInst();
  if (!i0)
    return 0;

  const unsigned op0 = i0->getOpcode();

  // Single-instruction endings
  if (op0 == opRET_)
    return 1;
  if (trapOpcodes_.contains(op0))
    return 1;

  if (insns.size() < 2)
    return 0;

  std::size_t pos = 0;

  // Optional: autiasp (authenticate return address before restoring)
  if (op0 == opAUTIASP_)
    pos = 1;

  // ldp x29, x30, [sp], #N  (LDPXpost)
  // or  ldp x29, x30, [sp, #M]  (LDPXi)
  if (pos >= insns.size())
    return 0;

  const auto *ldpMC = insns[pos]->mcInst();
  if (!ldpMC)
    return 0;

  if (const unsigned opLdp = ldpMC->getOpcode();
      (opLdp != opLDPXpost_ && opLdp != opLDPXi_) ||
      !mcHasReg(*ldpMC, regX29_) || !mcHasReg(*ldpMC, regX30_) ||
      !mcHasReg(*ldpMC, regSP64_))
    return 0;

  pos += 1;

  // Optional: add sp, sp, #N  (explicit deallocation when using LDPXi)
  if (pos < insns.size()) {
    if (const auto *addMC = insns[pos]->mcInst();
        addMC && addMC->getOpcode() == opADDXri_ &&
        addMC->getNumOperands() >= 2 && addMC->getOperand(0).isReg() &&
        addMC->getOperand(0).getReg() == regSP64_ &&
        addMC->getOperand(1).isReg() &&
        addMC->getOperand(1).getReg() == regSP64_)
      pos += 1;
  }

  // ret is mandatory to close the epilogue
  if (pos >= insns.size())
    return 0;

  if (const auto *retMC = insns[pos]->mcInst();
      retMC && retMC->getOpcode() == opRET_)
    return pos + 1;

  return 0;
}

std::size_t dragon::ARMInfo::arm32IsCommonPrologue(
        std::span<const Instruction *> insns) const {
  if (insns.empty())
    return 0;

  const auto *mc0 = insns[0]->mcInst();
  if (!mc0)
    return 0;

  const unsigned op0 = mc0->getOpcode();

  // push {rX-rY, lr} or stmdb sp!, {rX-rY, lr} or tpush {rX-rY, lr}
  if ((op0 != opPUSH_ && op0 != opTPUSH_ && op0 != opSTMDB_) ||
      !mcHasReg(*mc0, regLR_))
    return 0;

  std::size_t pos = 1;

  if (pos < insns.size()) {
    const auto *addMC = insns[pos]->mcInst();
    if (addMC && addMC->getOpcode() == opADDri_ &&
        addMC->getNumOperands() >= 2 && addMC->getOperand(0).isReg() &&
        addMC->getOperand(0).getReg() == regR11_ &&
        addMC->getOperand(1).isReg() &&
        addMC->getOperand(1).getReg() == regSP32_) {
      pos += 1;
    }
  }

  return pos;
}

std::size_t dragon::ARMInfo::arm32IsCommonEpilogue(
        std::span<const Instruction *> insns) const {
  if (insns.empty())
    return 0;

  const auto *mc0 = insns[0]->mcInst();
  if (!mc0)
    return 0;
  const unsigned op0 = mc0->getOpcode();

  // bx lr / mov pc, lr / tBX_RET - single-instruction returns
  if (op0 == opBXRET_ || op0 == opMOVPCLR_ || op0 == opTBXRET_)
    return 1;

  // pop {rX-rY, pc} or tpop_ret {rX-rY, pc} or ldmia sp!, {rX-rY, pc}
  // - restoring PC directly terminates the function
  if ((op0 == opPOP_ || op0 == opTPOPRET_ || op0 == opLDMIA_) &&
      mcHasReg(*mc0, regPC_))
    return 1;

  if (insns.size() < 2)
    return 0;

  // pop {rX-rY, lr} or ldmia sp!, {rX-rY, lr} followed by bx lr
  if ((op0 != opPOP_ && op0 != opTPOP_ && op0 != opLDMIA_) ||
      !mcHasReg(*mc0, regLR_))
    return 0;

  const auto *mc1 = insns[1]->mcInst();
  if (!mc1)
    return 0;

  const unsigned op1 = mc1->getOpcode();
  if (op1 == opBXRET_ || op1 == opMOVPCLR_ || op1 == opTBXRET_)
    return 2;

  return 0;
}

bool dragon::ARMInfo::isTrap(const Instruction &insn) const {
  const auto *mc = insn.mcInst();
  if (!mc)
    return false;
  return trapOpcodes_.contains(mc->getOpcode());
}

bool dragon::ARMInfo::isNop(const Instruction &insn) const {
  const auto *mc = insn.mcInst();
  if (!mc)
    return false;
  return nopOpcodes_.contains(mc->getOpcode());
}
