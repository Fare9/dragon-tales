#include "disassemblers/x86/X86IGNILLifter.hpp"

#include <dragon/disassembler/BasicBlock.hpp>
#include <dragon/disassembler/Instruction.hpp>

#include <array>
#include <cmath>
#include <string_view>
#include <unordered_map>

#include <llvm/MC/MCInst.h>

// Authoritative IGNIL register address space for the x86 lifter.
// Entries are defined in x86registers.def via the REG(name, offset, width)
// macro.
static const std::unordered_map<std::string_view, dragon::ignil::Reg>
        kRegisterTable = {
#define REG(name, offset, width) {name, {offset, width, name}},
#include "x86registers.def"


#undef REG
};


dragon::x86::X86IGNILLifter::X86IGNILLifter(
        internal::Logger &logger, Architecture architecture,
        const llvm::MCInstrInfo &instrInfo,
        const llvm::MCRegisterInfo &regInfo) :
    logger_(logger), architecture_(architecture), instrInfo_(&instrInfo) {

  // Build LLVM register integer -> ignil::Reg mapping
  for (unsigned i = 0; i < regInfo.getNumRegs(); ++i) {
    std::string_view name = regInfo.getName(i);
    auto it = kRegisterTable.find(name);
    if (it != kRegisterTable.end())
      regMap_.emplace(i, it->second);
  }

  // Flags are not LLVM architectural registers; look them up directly in
  // kRegisterTable so we always get the right IGNIL address regardless of
  // what LLVM's register info contains.
  auto findFlagReg = [](std::string_view name) -> ignil::Reg {
    auto it = kRegisterTable.find(name);
    return it != kRegisterTable.end() ? it->second : ignil::Reg{0, 0};
  };
  // General-purpose registers that are needed structurally (RSP/ESP for
  // push/pop) are also real LLVM registers, so use regMap_ for those.
  auto findGprReg = [&](std::string_view name) -> ignil::Reg {
    for (unsigned i = 0; i < regInfo.getNumRegs(); ++i) {
      if (regInfo.getName(i) == name) {
        if (auto it = regMap_.find(i); it != regMap_.end())
          return it->second;
      }
    }
    return ignil::Reg{0, 0};
  };

  regCF_ = findFlagReg("CF");
  regPF_ = findFlagReg("PF");
  regAF_ = findFlagReg("AF");
  regZF_ = findFlagReg("ZF");
  regSF_ = findFlagReg("SF");
  regOF_ = findFlagReg("OF");
  regDF_ = findFlagReg("DF");
  regRSP_ = findGprReg("RSP");
  regESP_ = findGprReg("ESP");

  // Scan opcodes by name: map LLVM mnemonic string -> pointer-to-member field.
  using OpcField = unsigned X86IGNILLifter::*;
  static const std::unordered_map<std::string_view, OpcField> kOpcodeFields = {
          {"MOV64rr", &X86IGNILLifter::opMov64rr_},
          {"MOV32rr", &X86IGNILLifter::opMov32rr_},
          {"MOV64ri", &X86IGNILLifter::opMov64ri_},
          {"MOV32ri", &X86IGNILLifter::opMov32ri_},
          {"MOV64ri32", &X86IGNILLifter::opMov64ri32_},
          {"MOV64rm", &X86IGNILLifter::opMov64rm_},
          {"MOV32rm", &X86IGNILLifter::opMov32rm_},
          {"MOV64mr", &X86IGNILLifter::opMov64mr_},
          {"MOV32mr", &X86IGNILLifter::opMov32mr_},
          {"LEA64r", &X86IGNILLifter::opLea64r_},
          {"LEA32r", &X86IGNILLifter::opLea32r_},
          {"ADD64rr", &X86IGNILLifter::opAdd64rr_},
          {"ADD32rr", &X86IGNILLifter::opAdd32rr_},
          {"ADD64ri32", &X86IGNILLifter::opAdd64ri_},
          {"ADD64ri8", &X86IGNILLifter::opAdd64ri8_},
          {"ADD32ri", &X86IGNILLifter::opAdd32ri_},
          {"ADD32ri8", &X86IGNILLifter::opAdd32ri8_},
          {"SUB64rr", &X86IGNILLifter::opSub64rr_},
          {"SUB32rr", &X86IGNILLifter::opSub32rr_},
          {"SUB64ri32", &X86IGNILLifter::opSub64ri_},
          {"SUB64ri8", &X86IGNILLifter::opSub64ri8_},
          {"SUB32ri", &X86IGNILLifter::opSub32ri_},
          {"SUB32ri8", &X86IGNILLifter::opSub32ri8_},
          {"XOR64rr", &X86IGNILLifter::opXor64rr_},
          {"XOR32rr", &X86IGNILLifter::opXor32rr_},
          {"AND64rr", &X86IGNILLifter::opAnd64rr_},
          {"AND32rr", &X86IGNILLifter::opAnd32rr_},
          {"OR64rr", &X86IGNILLifter::opOr64rr_},
          {"OR32rr", &X86IGNILLifter::opOr32rr_},
          {"PUSH64r", &X86IGNILLifter::opPush64r_},
          {"PUSH32r", &X86IGNILLifter::opPush32r_},
          {"POP64r", &X86IGNILLifter::opPop64r_},
          {"POP32r", &X86IGNILLifter::opPop32r_},
          {"CMP64rr", &X86IGNILLifter::opCmp64rr_},
          {"CMP32rr", &X86IGNILLifter::opCmp32rr_},
          {"CMP64ri32", &X86IGNILLifter::opCmp64ri_},
          {"CMP64ri8", &X86IGNILLifter::opCmp64ri8_},
          {"CMP32ri", &X86IGNILLifter::opCmp32ri_},
          {"CMP32ri8", &X86IGNILLifter::opCmp32ri8_},
          {"TEST64rr", &X86IGNILLifter::opTest64rr_},
          {"TEST32rr", &X86IGNILLifter::opTest32rr_},
          {"JMP64r", &X86IGNILLifter::opJmp64r_},
          {"JMP_1", &X86IGNILLifter::opJmp_},
          {"JMP", &X86IGNILLifter::opJmp_},
          {"JMP64m", &X86IGNILLifter::opJmp64m_},
          {"JMP32m", &X86IGNILLifter::opJmp32m_},
          {"CALL64r", &X86IGNILLifter::opCall64r_},
          {"CALL64m", &X86IGNILLifter::opCall64m_},
          {"CALL32m", &X86IGNILLifter::opCall32m_},
          {"CALL64pcrel32", &X86IGNILLifter::opCall_},
          {"RET64", &X86IGNILLifter::opRet64_},
          {"RETL", &X86IGNILLifter::opRetL_},
  };

  for (unsigned i = 0; i < instrInfo.getNumOpcodes(); ++i) {
    if (auto it = kOpcodeFields.find(instrInfo.getName(i));
        it != kOpcodeFields.end())
      this->*(it->second) = i;
  }
}

dragon::ignil::Block
dragon::x86::X86IGNILLifter::liftBlock(const BasicBlock &block,
                                       const Graph &graph) const {
  ignil::Block result{block.startAddress()};
  std::uint32_t tempId = 0;

  for (const Instruction *insn: block.instructions()) {
    liftInstruction(*insn, result, tempId);
  }

  return result;
}

void dragon::x86::X86IGNILLifter::liftInstruction(const Instruction &insn,
                                                  ignil::Block &out,
                                                  std::uint32_t &tempId) const {
  const auto *mc = insn.mcInst();
  if (!mc)
    return;

  unsigned opc = mc->getOpcode();
  std::uint64_t addr = insn.address();

  if (opc == opMov64rr_ || opc == opMov32rr_ || opc == opMov64ri_ ||
      opc == opMov32ri_ || opc == opMov64ri32_)
    liftMov(*mc, addr, out, tempId);
  else if (opc == opMov64rm_ || opc == opMov32rm_)
    liftMovLoad(*mc, addr, out, tempId);
  else if (opc == opMov64mr_ || opc == opMov32mr_)
    liftMovStore(*mc, addr, out, tempId);
  else if (opc == opAdd64rr_ || opc == opAdd32rr_ || opc == opAdd64ri_ ||
           opc == opAdd32ri_ || opc == opAdd64ri8_ || opc == opAdd32ri8_)
    liftAdd(*mc, addr, out, tempId);
  else if (opc == opSub64rr_ || opc == opSub32rr_ || opc == opSub64ri_ ||
           opc == opSub32ri_ || opc == opSub64ri8_ || opc == opSub32ri8_)
    liftSub(*mc, addr, out, tempId);
  else if (opc == opXor64rr_ || opc == opXor32rr_)
    liftXor(*mc, addr, out, tempId);
  else if (opc == opAnd64rr_ || opc == opAnd32rr_)
    liftAnd(*mc, addr, out, tempId);
  else if (opc == opOr64rr_ || opc == opOr32rr_)
    liftOr(*mc, addr, out, tempId);
  else if (opc == opPush64r_ || opc == opPush32r_)
    liftPush(*mc, addr, out, tempId);
  else if (opc == opPop64r_ || opc == opPop32r_)
    liftPop(*mc, addr, out, tempId);
  else if (opc == opLea64r_ || opc == opLea32r_)
    liftLea(*mc, addr, out, tempId);
  else if (opc == opCmp64rr_ || opc == opCmp32rr_ || opc == opCmp64ri_ ||
           opc == opCmp32ri_ || opc == opCmp64ri8_ || opc == opCmp32ri8_)
    liftCmp(*mc, addr, out, tempId);
  else if (opc == opTest64rr_ || opc == opTest32rr_)
    liftTest(*mc, addr, out, tempId);
  else if (opc == opCall_ || opc == opCall64r_)
    liftCall(*mc, addr, insn.size(), out, tempId);
  else if (opc == opRet64_ || opc == opRetL_)
    liftRet(*mc, addr, out, tempId);
  else if (opc == opJmp64m_ || opc == opJmp32m_)
    liftJmpMem(*mc, addr, insn.size(), out, tempId);
  else if (opc == opCall64m_ || opc == opCall32m_)
    liftCallMem(*mc, addr, insn.size(), out, tempId);
  else {
    const auto &desc = instrInfo_->get(opc);
    if (desc.isConditionalBranch())
      liftJcc(*mc, addr, insn.size(), out, tempId);
    else if (desc.isBranch())
      liftJmp(*mc, addr, insn.size(), out, tempId);
  }
  // Unknown - leave unlisted instructions as gaps (no UNDEF emitted for
  // instructions with no IGNIL-visible side effects, e.g. prefetch, fence)
}

void dragon::x86::X86IGNILLifter::liftMov(const llvm::MCInst &mc,
                                          std::uint64_t addr, ignil::Block &out,
                                          std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftMov in addr {:#x} no dst", addr);
    return;
  }

  const auto &srcOp = mc.getOperand(1);
  if (srcOp.isImm()) {
    auto imm = static_cast<std::uint64_t>(srcOp.getImm());
    out.append(
            {ignil::Opcode::COPY, *dst, {ignil::Const{imm, dst->width}}, addr});
  } else {
    auto src = toReg(srcOp.getReg());
    if (!src) {
      emitUndef(*dst, addr, out);
      return;
    }
    out.append({ignil::Opcode::COPY, *dst, {*src}, addr});
  }
}
void dragon::x86::X86IGNILLifter::liftMovLoad(const llvm::MCInst &mc,
                                              std::uint64_t addr,
                                              ignil::Block &out,
                                              std::uint32_t &tmp) const {
  // op[0]=dst, op[1..5]=mem(base,scale,index,disp,seg)
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftMovLoad in addr {:#x} no dst", addr);
    return;
  }

  ignil::Width ptrWidth = (architecture_ == Architecture::X86_64) ? 64 : 32;
  auto addrTemp = emitAddress(mc, 1, ptrWidth, addr, out, tmp);
  auto loaded = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::LOAD, loaded, {addrTemp}, addr});
  out.append({ignil::Opcode::COPY, *dst, {loaded}, addr});
}
void dragon::x86::X86IGNILLifter::liftMovStore(const llvm::MCInst &mc,
                                               std::uint64_t addr,
                                               ignil::Block &out,
                                               std::uint32_t &tmp) const {
  // op[0..4]=mem(base,scale,index,disp,seg), op[5]=src
  auto src = toReg(mc.getOperand(5).getReg());
  if (!src) {
    logger_.log("Error liftMovStore in addr {:#x} no src", addr);
    return;
  }

  ignil::Width ptrWidth = (architecture_ == Architecture::X86_64) ? 64 : 32;
  auto addrTemp = emitAddress(mc, 0, ptrWidth, addr, out, tmp);
  out.append({ignil::Opcode::STORE, std::nullopt, {addrTemp, *src}, addr});
}
void dragon::x86::X86IGNILLifter::liftAdd(const llvm::MCInst &mc,
                                          std::uint64_t addr, ignil::Block &out,
                                          std::uint32_t &tmp) const {
  // op[0]=dst(out), op[1]=src1(tied=dst), op[2]=src2(reg or imm)
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftAdd in addr {:#x} no dst", addr);
    return;
  }

  ignil::Value src;
  const auto &srcOp = mc.getOperand(2);
  if (srcOp.isImm()) {
    src = ignil::Const{static_cast<std::uint64_t>(srcOp.getImm()), dst->width};
  } else {
    auto srcReg = toReg(srcOp.getReg());
    if (!srcReg) {
      logger_.log("Error liftAdd in addr {:#x} no src", addr);
      emitUndef(*dst, addr, out);
      return;
    }
    src = *srcReg;
  }

  // Save original dst before overwrite so OF can compare pre- and post-signs.
  auto lhs = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::COPY, lhs, {*dst}, addr});

  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::ADD, result, {lhs, src}, addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});
  emitArithFlags(addr, out, tmp,
                 {
                         {X86Flag::CF, cfAddOverflow(result, src, addr)},
                         {X86Flag::ZF, zfEqZero(result, addr)},
                         {X86Flag::SF, sfSignBit(result, addr)},
                         {X86Flag::OF, ofAddOverflow(lhs, src, result, addr)},
                 });
}
void dragon::x86::X86IGNILLifter::liftSub(const llvm::MCInst &mc,
                                          std::uint64_t addr, ignil::Block &out,
                                          std::uint32_t &tmp) const {
  // op[0]=dst(out), op[1]=src1(tied=dst), op[2]=src2(reg or imm)
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftSub in addr {:#x} no dst", addr);
    return;
  }

  ignil::Value src;
  const auto &srcOp = mc.getOperand(2);
  if (srcOp.isImm()) {
    src = ignil::Const{static_cast<std::uint64_t>(srcOp.getImm()), dst->width};
  } else {
    auto srcReg = toReg(srcOp.getReg());
    if (!srcReg) {
      logger_.log("Error liftSub in addr {:#x} no src", addr);
      emitUndef(*dst, addr, out);
      return;
    }
    src = *srcReg;
  }

  // Save original dst before overwrite so CF/OF see the pre-subtraction value.
  auto lhs = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::COPY, lhs, {*dst}, addr});

  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::SUB, result, {lhs, src}, addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});
  emitArithFlags(addr, out, tmp,
                 {
                         {X86Flag::CF, cfSubBorrow(lhs, src, addr)},
                         {X86Flag::ZF, zfEqZero(result, addr)},
                         {X86Flag::SF, sfSignBit(result, addr)},
                         {X86Flag::OF, ofSubOverflow(lhs, src, result, addr)},
                 });
}
void dragon::x86::X86IGNILLifter::liftXor(const llvm::MCInst &mc,
                                          std::uint64_t addr, ignil::Block &out,
                                          std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  auto src = toReg(mc.getOperand(2).getReg());

  if (!dst) {
    logger_.log("Error liftXor in addr {:#x} no dst", addr);
    return;
  }
  if (!src) {
    logger_.log("Error liftXor in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }
  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::XOR, result, {*dst, *src}, addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});
  emitArithFlags(addr, out, tmp,
                 {
                         {X86Flag::CF, cfAlwaysZero(addr)},
                         {X86Flag::ZF, zfEqZero(result, addr)},
                         {X86Flag::SF, sfSignBit(result, addr)},
                         {X86Flag::OF, ofAlwaysZero(addr)},
                 });
}


void dragon::x86::X86IGNILLifter::liftAnd(const llvm::MCInst &mc,
                                          std::uint64_t addr, ignil::Block &out,
                                          std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  auto src = toReg(mc.getOperand(2).getReg());

  if (!dst) {
    logger_.log("Error liftAnd in addr {:#x} no dst", addr);
    return;
  }
  if (!src) {
    logger_.log("Error liftAnd in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }
  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::AND, result, {*dst, *src}, addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});
  emitArithFlags(addr, out, tmp,
                 {
                         {X86Flag::CF, cfAlwaysZero(addr)},
                         {X86Flag::ZF, zfEqZero(result, addr)},
                         {X86Flag::SF, sfSignBit(result, addr)},
                         {X86Flag::OF, ofAlwaysZero(addr)},
                 });
}
void dragon::x86::X86IGNILLifter::liftOr(const llvm::MCInst &mc,
                                         std::uint64_t addr, ignil::Block &out,
                                         std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  auto src = toReg(mc.getOperand(2).getReg());

  if (!dst) {
    logger_.log("Error liftOr in addr {:#x} no dst", addr);
    return;
  }
  if (!src) {
    logger_.log("Error liftOr in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }
  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::OR, result, {*dst, *src}, addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});
  emitArithFlags(addr, out, tmp,
                 {
                         {X86Flag::CF, cfAlwaysZero(addr)},
                         {X86Flag::ZF, zfEqZero(result, addr)},
                         {X86Flag::SF, sfSignBit(result, addr)},
                         {X86Flag::OF, ofAlwaysZero(addr)},
                 });
}
void dragon::x86::X86IGNILLifter::liftPush(const llvm::MCInst &mc,
                                           std::uint64_t addr,
                                           ignil::Block &out,
                                           std::uint32_t &tmp) const {
  auto src = toReg(mc.getOperand(0).getReg());
  if (!src) {
    logger_.log("Error liftPush in addr {:#x} no src", addr);
    return;
  }

  const ignil::Width ptrWidth =
          (architecture_ == Architecture::X86_64) ? 64 : 32;
  ignil::Reg sp = (architecture_ == Architecture::X86_64) ? regRSP_ : regESP_;
  const ignil::Width srcBytes = src->width / 8;

  // First SP -= size
  auto newSP = newTemp(tmp, ptrWidth);
  out.append({ignil::Opcode::SUB,
              newSP,
              {sp, ignil::Const{srcBytes, ptrWidth}},
              addr});
  out.append({ignil::Opcode::COPY, sp, {newSP}, addr});

  // memory[RSP] = src
  out.append({ignil::Opcode::STORE, std::nullopt, {sp, *src}, addr});
}
void dragon::x86::X86IGNILLifter::liftPop(const llvm::MCInst &mc,
                                          std::uint64_t addr, ignil::Block &out,
                                          std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftPop in addr {:#x} no dst", addr);
    return;
  }

  const ignil::Width ptrWidth =
          (architecture_ == Architecture::X86_64) ? 64 : 32;
  ignil::Reg sp = (architecture_ == Architecture::X86_64) ? regRSP_ : regESP_;
  const ignil::Width dstBytes = dst->width / 8;

  // dst = memory[RSP]
  auto loaded = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::LOAD, loaded, {sp}, addr});
  out.append({ignil::Opcode::COPY, *dst, {loaded}, addr});

  // RSP += size
  auto newSP = newTemp(tmp, ptrWidth);
  out.append({ignil::Opcode::ADD,
              newSP,
              {sp, ignil::Const{dstBytes, ptrWidth}},
              addr});
  out.append({ignil::Opcode::COPY, sp, {newSP}, addr});
}

void dragon::x86::X86IGNILLifter::liftLea(const llvm::MCInst &mc,
                                          std::uint64_t addr, ignil::Block &out,
                                          std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftLea in addr {:#x} no dst", addr);
    return;
  }

  const ignil::Width ptrWidth =
          (architecture_ == Architecture::X86_64) ? 64 : 32;
  auto addrTemp = emitAddress(mc, 1, ptrWidth, addr, out, tmp);
  out.append({ignil::Opcode::COPY, *dst, {addrTemp}, addr});
}
void dragon::x86::X86IGNILLifter::liftCmp(const llvm::MCInst &mc,
                                          std::uint64_t addr, ignil::Block &out,
                                          std::uint32_t &tmp) const {
  // CMP has no output: op[0]=lhs, op[1]=rhs(reg or imm)
  auto lhs = toReg(mc.getOperand(0).getReg());
  if (!lhs) {
    logger_.log("Error liftCmp in addr {:#x} no lhs", addr);
    return;
  }

  ignil::Value rhs;
  if (const auto &rhsOp = mc.getOperand(1); rhsOp.isImm()) {
    rhs = ignil::Const{static_cast<std::uint64_t>(rhsOp.getImm()), lhs->width};
  } else {
    auto rhsReg = toReg(rhsOp.getReg());
    if (!rhsReg) {
      logger_.log("Error liftCmp in addr {:#x} no rhs", addr);
      return;
    }
    rhs = *rhsReg;
  }

  // CMP is SUB that discards the result but keeps all flags.
  // lhs is not overwritten so its Reg can be used directly in CF/OF factories.
  auto result = newTemp(tmp, lhs->width);
  out.append({ignil::Opcode::SUB, result, {*lhs, rhs}, addr});
  emitArithFlags(addr, out, tmp,
                 {
                         {X86Flag::CF, cfSubBorrow(*lhs, rhs, addr)},
                         {X86Flag::ZF, zfEqZero(result, addr)},
                         {X86Flag::SF, sfSignBit(result, addr)},
                         {X86Flag::OF, ofSubOverflow(*lhs, rhs, result, addr)},
                 });
}
void dragon::x86::X86IGNILLifter::liftTest(const llvm::MCInst &mc,
                                           std::uint64_t addr,
                                           ignil::Block &out,
                                           std::uint32_t &tmp) const {
  auto lhs = toReg(mc.getOperand(0).getReg());
  auto rhs = toReg(mc.getOperand(1).getReg());
  if (!lhs || !rhs) {
    logger_.log("Error liftTest in addr {:#x} no lhs or rhs", addr);
    return;
  }

  // TEST is AND that discards the result but keeps flags; CF and OF always 0.
  auto result = newTemp(tmp, lhs->width);
  out.append({ignil::Opcode::AND, result, {*lhs, *rhs}, addr});
  emitArithFlags(addr, out, tmp,
                 {
                         {X86Flag::CF, cfAlwaysZero(addr)},
                         {X86Flag::ZF, zfEqZero(result, addr)},
                         {X86Flag::SF, sfSignBit(result, addr)},
                         {X86Flag::OF, ofAlwaysZero(addr)},
                 });
}
void dragon::x86::X86IGNILLifter::liftJcc(const llvm::MCInst &mc,
                                          std::uint64_t addr,
                                          std::uint8_t insnSize,
                                          ignil::Block &out,
                                          std::uint32_t &tmp) const {
  // LLVM 15+ unifies all Jcc into JCC_1/JCC_4 with layout:
  //   op[0] = signed PC-relative offset (imm)
  //   op[1] = condition code (imm, LLVM x86 CondCode enum 0-15)
  const std::int64_t offset = mc.getOperand(0).getImm();
  const auto taken = static_cast<std::uint64_t>(
          static_cast<std::int64_t>(addr) + insnSize + offset);
  const std::uint64_t fallthrough = addr + insnSize;

  unsigned condCode = (mc.getNumOperands() >= 2 && mc.getOperand(1).isImm())
                              ? static_cast<unsigned>(mc.getOperand(1).getImm())
                              : 0;

  auto cond = emitCondition(condCode, addr, out, tmp);
  out.append({ignil::Opcode::CJMP,
              std::nullopt,
              {cond, ignil::Const{taken, 64}, ignil::Const{fallthrough, 64}},
              addr});
}

void dragon::x86::X86IGNILLifter::liftJmp(const llvm::MCInst &mc,
                                          const std::uint64_t addr,
                                          std::uint8_t insnSize,
                                          ignil::Block &out,
                                          std::uint32_t &tmp) const {
  if (const auto &op = mc.getOperand(0); op.isImm()) {
    const auto target = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(addr) + insnSize + op.getImm());
    out.append({ignil::Opcode::JMP,
                std::nullopt,
                {ignil::Const{target, 64}},
                addr});
  } else if (op.isReg()) {
    auto targetReg = toReg(op.getReg());
    if (!targetReg) {
      logger_.log("Error liftJmp in addr {:#x} no target register", addr);
      return;
    }
    out.append({ignil::Opcode::JMP, std::nullopt, {*targetReg}, addr});
  }
}
void dragon::x86::X86IGNILLifter::liftJmpMem(const llvm::MCInst &mc,
                                             std::uint64_t addr,
                                             std::uint8_t insnSize,
                                             ignil::Block &out,
                                             std::uint32_t &tmp) const {
  ignil::Width ptrWidth = (architecture_ == Architecture::X86_64) ? 64 : 32;
  // generate an address from the instruction
  auto addrTemp = emitAddress(mc, 0, ptrWidth, addr, out, tmp);
  // generate a target that will be used to store the address
  auto target = newTemp(tmp, ptrWidth);
  out.append({ignil::Opcode::LOAD, target, {addrTemp}, addr});
  out.append({ignil::Opcode::JMP, std::nullopt, {target}, addr});
}
void dragon::x86::X86IGNILLifter::liftCallMem(const llvm::MCInst &mc,
                                              std::uint64_t addr,
                                              std::uint8_t insnSize,
                                              ignil::Block &out,
                                              std::uint32_t &tmp) const {
  ignil::Width ptrWidth = (architecture_ == Architecture::X86_64) ? 64 : 32;
  // generate an address from the instruction
  auto addrTemp = emitAddress(mc, 0, ptrWidth, addr, out, tmp);
  // generate a target that will be used to store the address
  auto target = newTemp(tmp, ptrWidth);
  out.append({ignil::Opcode::LOAD, target, {addrTemp}, addr});
  out.append({ignil::Opcode::CALL, std::nullopt, {target}, addr});
}


void dragon::x86::X86IGNILLifter::liftCall(const llvm::MCInst &mc,
                                           std::uint64_t addr,
                                           std::uint8_t insnSize,
                                           ignil::Block &out,
                                           std::uint32_t &tmp) const {
  if (const auto &op = mc.getOperand(0); op.isImm()) {
    // Direct call: pc-relative offset from end of instruction
    const auto target = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(addr) + insnSize + op.getImm());
    out.append({ignil::Opcode::CALL,
                std::nullopt,
                {ignil::Const{target, 64}},
                addr});
  } else if (op.isReg()) {
    // Indirect call through register
    auto targetReg = toReg(op.getReg());
    if (!targetReg) {
      logger_.log("Error liftCall in addr {:#x} no target register", addr);
      return;
    }
    out.append({ignil::Opcode::CALL, std::nullopt, {*targetReg}, addr});
  }
}

void dragon::x86::X86IGNILLifter::liftRet(const llvm::MCInst &mc,
                                          std::uint64_t addr, ignil::Block &out,
                                          std::uint32_t &tmp) const {
  out.append({ignil::Opcode::RET, std::nullopt, {}, addr});
}
std::optional<dragon::ignil::Reg>
dragon::x86::X86IGNILLifter::toReg(unsigned llvmReg) const {
  auto it = regMap_.find(llvmReg);
  if (it == regMap_.end())
    return std::nullopt;
  return it->second;
}

dragon::ignil::Temp
dragon::x86::X86IGNILLifter::newTemp(std::uint32_t &tempId,
                                     ignil::Width width) const {
  return ignil::Temp{tempId++, width};
}

dragon::ignil::Temp dragon::x86::X86IGNILLifter::emitAddress(
        const llvm::MCInst &mc, unsigned opStart, ignil::Width ptrWidth,
        std::uint64_t addr, ignil::Block &out, std::uint32_t &tmp) const {
  // x86 memory operand layout: base, scale, index, disp, seg
  auto base = toReg(mc.getOperand(opStart + 0).getReg());
  const auto scale = static_cast<unsigned>(mc.getOperand(opStart + 1).getImm());
  auto index = toReg(mc.getOperand(opStart + 2).getReg());
  const std::int64_t disp = mc.getOperand(opStart + 3).getImm();

  // Accumulate into temps. Start with base or zero.
  auto result = newTemp(tmp, ptrWidth);
  if (base)
    out.append({ignil::Opcode::COPY, result, {*base}, addr});
  else
    out.append(
            {ignil::Opcode::COPY, result, {ignil::Const{0, ptrWidth}}, addr});

  // Add index*scale if an index register is present.
  if (index) {
    auto term = newTemp(tmp, ptrWidth);
    if (scale > 1) {
      // scale is always a power of two (1/2/4/8); represent as SHL.
      const ignil::Width shift = (scale == 2) ? 1u : (scale == 4) ? 2u : 3u;
      out.append({ignil::Opcode::SHL,
                  term,
                  {*index, ignil::Const{shift, 8}},
                  addr});
    } else {
      out.append({ignil::Opcode::COPY, term, {*index}, addr});
    }
    auto added = newTemp(tmp, ptrWidth);
    out.append({ignil::Opcode::ADD, added, {result, term}, addr});
    result = added;
  }

  // Add displacement if non-zero.
  if (disp != 0) {
    auto withDisp = newTemp(tmp, ptrWidth);
    out.append(
            {ignil::Opcode::ADD,
             withDisp,
             {result, ignil::Const{static_cast<std::uint64_t>(disp), ptrWidth}},
             addr});
    result = withDisp;
  }

  return result;
}

dragon::ignil::Temp dragon::x86::X86IGNILLifter::emitCondition(
        unsigned condCode, std::uint64_t addr, ignil::Block &out,
        std::uint32_t &tmp) const {
  // LLVM x86 CondCode enum (X86BaseInfo.h):
  //   0=O,1=NO,2=B,3=AE,4=E,5=NE,6=BE,7=A,8=S,9=NS,12=L,13=GE,14=LE,15=G
  auto flagSet = [&](ignil::Reg flag) -> ignil::Temp {
    auto t = newTemp(tmp, 8);
    out.append({ignil::Opcode::COPY, t, {flag}, addr});
    auto cond = newTemp(tmp, 1);
    out.append({ignil::Opcode::NEQ, cond, {t, ignil::Const{0, 8}}, addr});
    return cond;
  };
  auto flagClear = [&](ignil::Reg flag) -> ignil::Temp {
    auto t = newTemp(tmp, 8);
    out.append({ignil::Opcode::COPY, t, {flag}, addr});
    auto cond = newTemp(tmp, 1);
    out.append({ignil::Opcode::EQ, cond, {t, ignil::Const{0, 8}}, addr});
    return cond;
  };

  switch (condCode) {
    case 2:
      return flagSet(regCF_); // B  / JB  : CF=1
    case 3:
      return flagClear(regCF_); // AE / JAE : CF=0
    case 4:
      return flagSet(regZF_); // E  / JE  : ZF=1
    case 5:
      return flagClear(regZF_); // NE / JNE : ZF=0
    case 8:
      return flagSet(regSF_); // S  / JS  : SF=1
    case 9:
      return flagClear(regSF_); // NS / JNS : SF=0
    case 6: { // BE / JBE : CF=1 OR ZF=1
      auto cf = flagSet(regCF_), zf = flagSet(regZF_);
      auto cond = newTemp(tmp, 1);
      out.append({ignil::Opcode::OR, cond, {cf, zf}, addr});
      return cond;
    }
    case 7: { // A  / JA  : CF=0 AND ZF=0
      auto cfClr = flagClear(regCF_), zfClr = flagClear(regZF_);
      auto cond = newTemp(tmp, 1);
      out.append({ignil::Opcode::AND, cond, {cfClr, zfClr}, addr});
      return cond;
    }
    case 12: { // L  / JL  : SF != OF
      auto sf = newTemp(tmp, 8), of = newTemp(tmp, 8);
      out.append({ignil::Opcode::COPY, sf, {regSF_}, addr});
      out.append({ignil::Opcode::COPY, of, {regOF_}, addr});
      auto cond = newTemp(tmp, 1);
      out.append({ignil::Opcode::NEQ, cond, {sf, of}, addr});
      return cond;
    }
    case 13: { // GE / JGE : SF == OF
      auto sf = newTemp(tmp, 8), of = newTemp(tmp, 8);
      out.append({ignil::Opcode::COPY, sf, {regSF_}, addr});
      out.append({ignil::Opcode::COPY, of, {regOF_}, addr});
      auto cond = newTemp(tmp, 1);
      out.append({ignil::Opcode::EQ, cond, {sf, of}, addr});
      return cond;
    }
    case 14: { // LE / JLE : ZF=1 OR (SF != OF)
      auto zf = flagSet(regZF_);
      auto sf = newTemp(tmp, 8), of = newTemp(tmp, 8);
      out.append({ignil::Opcode::COPY, sf, {regSF_}, addr});
      out.append({ignil::Opcode::COPY, of, {regOF_}, addr});
      auto sfNeOf = newTemp(tmp, 1);
      out.append({ignil::Opcode::NEQ, sfNeOf, {sf, of}, addr});
      auto cond = newTemp(tmp, 1);
      out.append({ignil::Opcode::OR, cond, {zf, sfNeOf}, addr});
      return cond;
    }
    case 15: { // G  / JG  : ZF=0 AND SF == OF
      auto zfClr = flagClear(regZF_);
      auto sf = newTemp(tmp, 8), of = newTemp(tmp, 8);
      out.append({ignil::Opcode::COPY, sf, {regSF_}, addr});
      out.append({ignil::Opcode::COPY, of, {regOF_}, addr});
      auto sfEqOf = newTemp(tmp, 1);
      out.append({ignil::Opcode::EQ, sfEqOf, {sf, of}, addr});
      auto cond = newTemp(tmp, 1);
      out.append({ignil::Opcode::AND, cond, {zfClr, sfEqOf}, addr});
      return cond;
    }
    default: {
      auto undef = newTemp(tmp, 1);
      out.append({ignil::Opcode::UNDEF, undef, {}, addr});
      return undef;
    }
  }
}

void dragon::x86::X86IGNILLifter::emitUndef(ignil::Reg dst, std::uint64_t addr,
                                            ignil::Block &out) const {
  ignil::Op op;
  op.opcode = ignil::Opcode::UNDEF;
  op.dst = dst;
  op.address = addr;
  out.append(std::move(op));
}

void dragon::x86::X86IGNILLifter::emitArithFlags(
        std::uint64_t addr, ignil::Block &out, std::uint32_t &tmp,
        const std::map<X86Flag, FlagSetter> &flagMap) const {
  const std::array<std::pair<X86Flag, ignil::Reg>, 7> allFlags = {{
          {X86Flag::CF, regCF_},
          {X86Flag::PF, regPF_},
          {X86Flag::AF, regAF_},
          {X86Flag::ZF, regZF_},
          {X86Flag::SF, regSF_},
          {X86Flag::OF, regOF_},
          {X86Flag::DF, regDF_},
  }};

  for (const auto &[flag, reg]: allFlags) {
    auto it = flagMap.find(flag);
    if (it != flagMap.end())
      it->second(out, tmp);
    else
      emitUndef(reg, addr, out);
  }
}

dragon::x86::FlagSetter
dragon::x86::X86IGNILLifter::zfEqZero(ignil::Value value,
                                      std::uint64_t addr) const {
  return [this, value, addr](ignil::Block &out, std::uint32_t &tmp) {
    auto t = newTemp(tmp, 1);
    out.append({ignil::Opcode::EQ,
                t,
                {value, ignil::Const{0, ignil::widthOf(value)}},
                addr});
    out.append({ignil::Opcode::COPY, regZF_, {t}, addr});
  };
}

dragon::x86::FlagSetter
dragon::x86::X86IGNILLifter::sfSignBit(ignil::Value value,
                                       std::uint64_t addr) const {
  return [this, value, addr](ignil::Block &out, std::uint32_t &tmp) {
    auto t = newTemp(tmp, 1);
    out.append({ignil::Opcode::SLT,
                t,
                {value, ignil::Const{0, ignil::widthOf(value)}},
                addr});
    out.append({ignil::Opcode::COPY, regSF_, {t}, addr});
  };
}

dragon::x86::FlagSetter
dragon::x86::X86IGNILLifter::cfAlwaysZero(std::uint64_t addr) const {
  return [this, addr](ignil::Block &out, std::uint32_t &) {
    out.append({ignil::Opcode::COPY, regCF_, {ignil::Const{0, 1}}, addr});
  };
}

dragon::x86::FlagSetter
dragon::x86::X86IGNILLifter::ofAlwaysZero(std::uint64_t addr) const {
  return [this, addr](ignil::Block &out, std::uint32_t &) {
    out.append({ignil::Opcode::COPY, regOF_, {ignil::Const{0, 1}}, addr});
  };
}

dragon::x86::FlagSetter dragon::x86::X86IGNILLifter::cfAddOverflow(
        ignil::Value result, ignil::Value addend, std::uint64_t addr) const {
  // Unsigned overflow: result wrapped around  ⟺  result < addend (unsigned)
  return [this, result, addend, addr](ignil::Block &out, std::uint32_t &tmp) {
    auto t = newTemp(tmp, 1);
    out.append({ignil::Opcode::ULT, t, {result, addend}, addr});
    out.append({ignil::Opcode::COPY, regCF_, {t}, addr});
  };
}

dragon::x86::FlagSetter
dragon::x86::X86IGNILLifter::cfSubBorrow(ignil::Value lhs, ignil::Value rhs,
                                         std::uint64_t addr) const {
  // Unsigned borrow: subtraction required borrow  ⟺  lhs < rhs (unsigned)
  return [this, lhs, rhs, addr](ignil::Block &out, std::uint32_t &tmp) {
    auto t = newTemp(tmp, 1);
    out.append({ignil::Opcode::ULT, t, {lhs, rhs}, addr});
    out.append({ignil::Opcode::COPY, regCF_, {t}, addr});
  };
}

dragon::x86::FlagSetter
dragon::x86::X86IGNILLifter::ofAddOverflow(ignil::Value lhs, ignil::Value rhs,
                                           ignil::Value result,
                                           std::uint64_t addr) const {
  // Signed overflow when both inputs have the same sign but the result has the
  // opposite sign: OF = (sign(lhs) == sign(rhs)) AND (sign(result) !=
  // sign(lhs))
  return [this, lhs, rhs, result, addr](ignil::Block &out, std::uint32_t &tmp) {
    ignil::Const zero{0, ignil::widthOf(lhs)};
    auto sa = newTemp(tmp, 1);
    out.append({ignil::Opcode::SLT, sa, {lhs, zero}, addr});
    auto sb = newTemp(tmp, 1);
    out.append({ignil::Opcode::SLT, sb, {rhs, zero}, addr});
    auto sr = newTemp(tmp, 1);
    out.append({ignil::Opcode::SLT, sr, {result, zero}, addr});
    auto sameSign = newTemp(tmp, 1);
    out.append({ignil::Opcode::EQ, sameSign, {sa, sb}, addr});
    auto diffSign = newTemp(tmp, 1);
    out.append({ignil::Opcode::NEQ, diffSign, {sr, sa}, addr});
    auto of = newTemp(tmp, 1);
    out.append({ignil::Opcode::AND, of, {sameSign, diffSign}, addr});
    out.append({ignil::Opcode::COPY, regOF_, {of}, addr});
  };
}

dragon::x86::FlagSetter
dragon::x86::X86IGNILLifter::ofSubOverflow(ignil::Value lhs, ignil::Value rhs,
                                           ignil::Value result,
                                           std::uint64_t addr) const {
  // Signed overflow when inputs have different signs and result sign differs
  // from lhs: OF = (sign(lhs) != sign(rhs)) AND (sign(result) != sign(lhs))
  return [this, lhs, rhs, result, addr](ignil::Block &out, std::uint32_t &tmp) {
    ignil::Const zero{0, ignil::widthOf(lhs)};
    auto sa = newTemp(tmp, 1);
    out.append({ignil::Opcode::SLT, sa, {lhs, zero}, addr});
    auto sb = newTemp(tmp, 1);
    out.append({ignil::Opcode::SLT, sb, {rhs, zero}, addr});
    auto sr = newTemp(tmp, 1);
    out.append({ignil::Opcode::SLT, sr, {result, zero}, addr});
    auto diffAB = newTemp(tmp, 1);
    out.append({ignil::Opcode::NEQ, diffAB, {sa, sb}, addr});
    auto diffRA = newTemp(tmp, 1);
    out.append({ignil::Opcode::NEQ, diffRA, {sr, sa}, addr});
    auto of = newTemp(tmp, 1);
    out.append({ignil::Opcode::AND, of, {diffAB, diffRA}, addr});
    out.append({ignil::Opcode::COPY, regOF_, {of}, addr});
  };
}
