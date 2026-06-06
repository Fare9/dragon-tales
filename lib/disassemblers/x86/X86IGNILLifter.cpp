#include "disassemblers/x86/X86IGNILLifter.hpp"

#include <dragon/BasicBlock.hpp>
#include <dragon/Instruction.hpp>

#include <string_view>
#include <unordered_map>

#include <llvm/MC/MCInst.h>

// Authoritative IGNIL register address space for the x86 lifter.
// Entries are defined in x86registers.def via the REG(name, offset, width)
// macro.
static const std::unordered_map<std::string_view,
                                std::pair<std::uint32_t, dragon::ignil::Width>>
        kRegisterTable = {
#define REG(name, offset, width) {name, {offset, width}},
#include "x86registers.def"


#undef REG
};


dragon::x86::X86IGNILLifter::X86IGNILLifter(
        Architecture architecture, const llvm::MCInstrInfo &instrInfo,
        const llvm::MCRegisterInfo &regInfo) :
    architecture_(architecture), instrInfo_(&instrInfo) {

  // Build LLVM register integer -> ignil::Reg mapping
  for (unsigned i = 0; i < regInfo.getNumRegs(); ++i) {
    std::string_view name = regInfo.getName(i);
    auto it = kRegisterTable.find(name);
    if (it != kRegisterTable.end()) {
      auto [offset, width] = it->second;
      regMap_.emplace(i, ignil::Reg{offset, width});
    }
  }

  // Flags are not LLVM architectural registers; look them up directly in
  // kRegisterTable so we always get the right IGNIL address regardless of
  // what LLVM's register info contains.
  auto findFlagReg = [](std::string_view name) -> ignil::Reg {
    auto it = kRegisterTable.find(name);
    if (it != kRegisterTable.end())
      return ignil::Reg{it->second.first, it->second.second};
    return ignil::Reg{0, 0};
  };
  // General-purpose registers that are needed structurally (RSP/ESP for push/pop)
  // are also real LLVM registers, so use regMap_ for those.
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

  // Scan opcodes by name - same as with x86Info
  for (unsigned i = 0; i < instrInfo.getNumOpcodes(); ++i) {
    std::string_view name = instrInfo.getName(i);
    if (name == "MOV64rr")
      opMov64rr_ = i;
    else if (name == "MOV32rr")
      opMov32rr_ = i;
    else if (name == "MOV64ri")
      opMov64ri_ = i;
    else if (name == "MOV32ri")
      opMov32ri_ = i;
    else if (name == "LEA64r")
      opLea64r_ = i;
    else if (name == "LEA32r")
      opLea32r_ = i;
    else if (name == "ADD64rr")
      opAdd64rr_ = i;
    else if (name == "ADD32rr")
      opAdd32rr_ = i;
    else if (name == "ADD64ri32")
      opAdd64ri_ = i;
    else if (name == "ADD32ri")
      opAdd32ri_ = i;
    else if (name == "SUB64rr")
      opSub64rr_ = i;
    else if (name == "SUB32rr")
      opSub32rr_ = i;
    else if (name == "XOR64rr")
      opXor64rr_ = i;
    else if (name == "XOR32rr")
      opXor32rr_ = i;
    else if (name == "AND64rr")
      opAnd64rr_ = i;
    else if (name == "AND32rr")
      opAnd32rr_ = i;
    else if (name == "OR64rr")
      opOr64rr_ = i;
    else if (name == "OR32rr")
      opOr32rr_ = i;
    else if (name == "PUSH64r")
      opPush64r_ = i;
    else if (name == "POP64r")
      opPop64r_ = i;
    else if (name == "CMP64rr")
      opCmp64rr_ = i;
    else if (name == "CMP32rr")
      opCmp32rr_ = i;
    else if (name == "TEST64rr")
      opTest64rr_ = i;
    else if (name == "TEST32rr")
      opTest32rr_ = i;
    else if (name == "JMP64r")
      opJmp64r_ = i;
    else if (name == "JMP_1" || name == "JMP")
      opJmp_ = i;
    else if (name == "CALL64r")
      opCall64r_ = i;
    else if (name == "CALL64pcrel32")
      opCall_ = i;
    else if (name == "RET64")
      opRet64_ = i;
    else if (name == "RETL")
      opRetL_ = i;
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

  if (opc == opMov64rr_ || opc == opMov32rr_)
    liftMov(*mc, addr, out, tempId);
  else if (opc == opAdd64rr_ || opc == opAdd32rr_)
    liftAdd(*mc, addr, out, tempId);
  else if (opc == opSub64rr_ || opc == opSub32rr_)
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
  else if (opc == opCmp64rr_ || opc == opCmp32rr_)
    liftCmp(*mc, addr, out, tempId);
  else if (opc == opTest64rr_ || opc == opTest32rr_)
    liftTest(*mc, addr, out, tempId);
  else if (opc == opRet64_ || opc == opRetL_)
    liftRet(*mc, addr, out, tempId);
  else {
    const auto &desc = instrInfo_->get(opc);
    if (desc.isConditionalBranch())
      liftJcc(*mc, addr, insn.size(), out, tempId);
    else if (desc.isBranch())
      liftJmp(*mc, addr, insn.size(), out, tempId);
  }
  // Unknown — leave unlisted instructions as gaps (no UNDEF emitted for
  // instructions with no IGNIL-visible side effects, e.g. prefetch, fence)
}
void dragon::x86::X86IGNILLifter::liftMov(const llvm::MCInst &mc,
                                          std::uint64_t addr, ignil::Block &out,
                                          std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  auto src = toReg(mc.getOperand(1).getReg());
  if (!dst || !src) {
    emitUndef(*dst, addr, out);
    return;
  }
  out.append({ignil::Opcode::COPY, *dst, {*src}, addr});
}
void dragon::x86::X86IGNILLifter::liftAdd(const llvm::MCInst &mc,
                                          std::uint64_t addr, ignil::Block &out,
                                          std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  auto src = toReg(mc.getOperand(2).getReg()); // op[1] is tied src1(=dst); src2 is op[2]
  if (!dst || !src) {
    emitUndef(*dst, addr, out);
    return;
  }

  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::ADD, result, {*dst, *src}, addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});
  emitArithFlags(result, addr, out, tmp);
}
void dragon::x86::X86IGNILLifter::liftSub(const llvm::MCInst &mc,
                                          std::uint64_t addr, ignil::Block &out,
                                          std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  auto src = toReg(mc.getOperand(2).getReg());
  if (!dst || !src) {
    emitUndef(*dst, addr, out);
    return;
  }

  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::SUB, result, {*dst, *src}, addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});
  emitArithFlags(result, addr, out, tmp);
}
void dragon::x86::X86IGNILLifter::liftXor(const llvm::MCInst &mc,
                                          std::uint64_t addr, ignil::Block &out,
                                          std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  auto src = toReg(mc.getOperand(2).getReg());

  if (!dst || !src) {
    emitUndef(*dst, addr, out);
    return;
  }
  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::XOR, result, {*dst, *src}, addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});
  emitArithFlags(result, addr, out, tmp);
}
void dragon::x86::X86IGNILLifter::liftAnd(const llvm::MCInst &mc,
                                          std::uint64_t addr, ignil::Block &out,
                                          std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  auto src = toReg(mc.getOperand(2).getReg());

  if (!dst || !src) {
    emitUndef(*dst, addr, out);
    return;
  }
  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::AND, result, {*dst, *src}, addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});
  emitArithFlags(result, addr, out, tmp);
}
void dragon::x86::X86IGNILLifter::liftOr(const llvm::MCInst &mc,
                                         std::uint64_t addr, ignil::Block &out,
                                         std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  auto src = toReg(mc.getOperand(2).getReg());

  if (!dst || !src) {
    emitUndef(*dst, addr, out);
    return;
  }
  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::OR, result, {*dst, *src}, addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});
  emitArithFlags(result, addr, out, tmp);
}


void dragon::x86::X86IGNILLifter::liftPush(const llvm::MCInst &mc,
                                           std::uint64_t addr,
                                           ignil::Block &out,
                                           std::uint32_t &tmp) const {
  auto src = toReg(mc.getOperand(0).getReg());
  if (!src)
    return;

  ignil::Width ptrWidth = (architecture_ == Architecture::X86_64) ? 64 : 32;
  ignil::Reg sp = (architecture_ == Architecture::X86_64) ? regRSP_ : regESP_;
  ignil::Width srcBytes = src->width / 8;

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
  if (!dst)
    return;

  ignil::Width ptrWidth = (architecture_ == Architecture::X86_64) ? 64 : 32;
  ignil::Reg sp = (architecture_ == Architecture::X86_64) ? regRSP_ : regESP_;
  ignil::Width dstBytes = dst->width / 8;

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
void dragon::x86::X86IGNILLifter::liftCmp(const llvm::MCInst &mc,
                                          std::uint64_t addr, ignil::Block &out,
                                          std::uint32_t &tmp) const {
  auto lhs = toReg(mc.getOperand(0).getReg());
  auto rhs = toReg(mc.getOperand(1).getReg());
  if (!lhs || !rhs)
    return;

  // CMP is SUB that discards the result but keeps the flags
  auto result = newTemp(tmp, lhs->width);
  out.append({ignil::Opcode::SUB, result, {*lhs, *rhs}, addr});
  emitArithFlags(result, addr, out, tmp);
}
void dragon::x86::X86IGNILLifter::liftTest(const llvm::MCInst &mc,
                                           std::uint64_t addr,
                                           ignil::Block &out,
                                           std::uint32_t &tmp) const {
  auto lhs = toReg(mc.getOperand(0).getReg());
  auto rhs = toReg(mc.getOperand(1).getReg());
  if (!lhs || !rhs)
    return;

  // TEST is AND that discards the result but keeps the flags
  auto result = newTemp(tmp, lhs->width);
  out.append({ignil::Opcode::AND, result, {*lhs, *rhs}, addr});
  emitArithFlags(result, addr, out, tmp);
}

dragon::ignil::Temp dragon::x86::X86IGNILLifter::emitAddress(
        const llvm::MCInst &mc, unsigned opStart, ignil::Width ptrWidth,
        std::uint64_t addr, ignil::Block &out, std::uint32_t &tmp) const {
  // x86 memory operand layout: base, scale, index, disp, seg
  auto base = toReg(mc.getOperand(opStart + 0).getReg());
  unsigned scale = static_cast<unsigned>(mc.getOperand(opStart + 1).getImm());
  auto index = toReg(mc.getOperand(opStart + 2).getReg());
  std::int64_t disp = mc.getOperand(opStart + 3).getImm();

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
      ignil::Width shift = (scale == 2) ? 1u : (scale == 4) ? 2u : 3u;
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

void dragon::x86::X86IGNILLifter::liftLea(const llvm::MCInst &mc,
                                          std::uint64_t addr, ignil::Block &out,
                                          std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst)
    return;

  ignil::Width ptrWidth = (architecture_ == Architecture::X86_64) ? 64 : 32;
  auto addrTemp = emitAddress(mc, 1, ptrWidth, addr, out, tmp);
  out.append({ignil::Opcode::COPY, *dst, {addrTemp}, addr});
}

dragon::ignil::Temp
dragon::x86::X86IGNILLifter::emitCondition(unsigned condCode,
                                           std::uint64_t addr,
                                           ignil::Block &out,
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
    case 2: return flagSet(regCF_);    // B  / JB  : CF=1
    case 3: return flagClear(regCF_);  // AE / JAE : CF=0
    case 4: return flagSet(regZF_);    // E  / JE  : ZF=1
    case 5: return flagClear(regZF_);  // NE / JNE : ZF=0
    case 8: return flagSet(regSF_);    // S  / JS  : SF=1
    case 9: return flagClear(regSF_);  // NS / JNS : SF=0
    case 6: {                          // BE / JBE : CF=1 OR ZF=1
      auto cf = flagSet(regCF_), zf = flagSet(regZF_);
      auto cond = newTemp(tmp, 1);
      out.append({ignil::Opcode::OR, cond, {cf, zf}, addr});
      return cond;
    }
    case 7: {                          // A  / JA  : CF=0 AND ZF=0
      auto cfClr = flagClear(regCF_), zfClr = flagClear(regZF_);
      auto cond = newTemp(tmp, 1);
      out.append({ignil::Opcode::AND, cond, {cfClr, zfClr}, addr});
      return cond;
    }
    case 12: {                         // L  / JL  : SF != OF
      auto sf = newTemp(tmp, 8), of = newTemp(tmp, 8);
      out.append({ignil::Opcode::COPY, sf, {regSF_}, addr});
      out.append({ignil::Opcode::COPY, of, {regOF_}, addr});
      auto cond = newTemp(tmp, 1);
      out.append({ignil::Opcode::NEQ, cond, {sf, of}, addr});
      return cond;
    }
    case 13: {                         // GE / JGE : SF == OF
      auto sf = newTemp(tmp, 8), of = newTemp(tmp, 8);
      out.append({ignil::Opcode::COPY, sf, {regSF_}, addr});
      out.append({ignil::Opcode::COPY, of, {regOF_}, addr});
      auto cond = newTemp(tmp, 1);
      out.append({ignil::Opcode::EQ, cond, {sf, of}, addr});
      return cond;
    }
    case 14: {                         // LE / JLE : ZF=1 OR (SF != OF)
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
    case 15: {                         // G  / JG  : ZF=0 AND SF == OF
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

void dragon::x86::X86IGNILLifter::liftJcc(const llvm::MCInst &mc,
                                          std::uint64_t addr,
                                          std::uint8_t insnSize,
                                          ignil::Block &out,
                                          std::uint32_t &tmp) const {
  // LLVM 15+ unifies all Jcc into JCC_1/JCC_4 with layout:
  //   op[0] = signed PC-relative offset (imm)
  //   op[1] = condition code (imm, LLVM x86 CondCode enum 0-15)
  std::int64_t offset = mc.getOperand(0).getImm();
  std::uint64_t taken = static_cast<std::uint64_t>(
          static_cast<std::int64_t>(addr) + insnSize + offset);
  std::uint64_t fallthrough = addr + insnSize;

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
                                          std::uint64_t addr,
                                          std::uint8_t insnSize,
                                          ignil::Block &out,
                                          std::uint32_t &tmp) const {
  const auto &op = mc.getOperand(0);
  if (op.isImm()) {
    std::uint64_t target = static_cast<std::uint64_t>(
            static_cast<std::int64_t>(addr) + insnSize + op.getImm());
    out.append({ignil::Opcode::JMP,
                std::nullopt,
                {ignil::Const{target, 64}},
                addr});
  } else if (op.isReg()) {
    auto targetReg = toReg(op.getReg());
    if (!targetReg)
      return;
    out.append({ignil::Opcode::JMP, std::nullopt, {*targetReg}, addr});
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

void dragon::x86::X86IGNILLifter::emitUndef(ignil::Reg dst, std::uint64_t addr,
                                            ignil::Block &out) const {
  ignil::Op op;
  op.opcode = ignil::Opcode::UNDEF;
  op.dst = dst;
  op.address = addr;
  out.append(std::move(op));
}

void dragon::x86::X86IGNILLifter::emitArithFlags(ignil::Value result,
                                                 std::uint64_t addr,
                                                 ignil::Block &out,
                                                 std::uint32_t &tmp) const {
  // ZF = (result == 0)
  auto zfTemp = newTemp(tmp, 1);
  out.append({ignil::Opcode::EQ,
              zfTemp,
              {result, ignil::Const{0, ignil::widthOf(result)}},
              addr});
  out.append({ignil::Opcode::COPY, regZF_, {zfTemp}, addr});

  // SF = (result < 0)  — signed less-than zero
  auto sfTemp = newTemp(tmp, 1);
  out.append({ignil::Opcode::SLT,
              sfTemp,
              {result, ignil::Const{0, ignil::widthOf(result)}},
              addr});
  out.append({ignil::Opcode::COPY, regSF_, {sfTemp}, addr});

  // CF, OF, AF — model as UNDEF (conservative; refine per-instruction later)
  emitUndef(regCF_, addr, out);
  emitUndef(regOF_, addr, out);
  emitUndef(regAF_, addr, out);

  // PF — parity of lowest byte; model as UNDEF for now
  emitUndef(regPF_, addr, out);
}
