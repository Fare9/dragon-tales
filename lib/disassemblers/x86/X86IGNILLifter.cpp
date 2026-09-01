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
  regRBP_ = findGprReg("RBP");
  regEBP_ = findGprReg("EBP");

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
          {"MOV64mi32", &X86IGNILLifter::opMov64mi_},
          {"MOV32mi", &X86IGNILLifter::opMov32mi_},
          {"LEA64r", &X86IGNILLifter::opLea64r_},
          {"LEA32r", &X86IGNILLifter::opLea32r_},
          {"ADD64rr", &X86IGNILLifter::opAdd64rr_},
          {"ADD32rr", &X86IGNILLifter::opAdd32rr_},
          {"ADD64ri32", &X86IGNILLifter::opAdd64ri_},
          {"ADD64ri8", &X86IGNILLifter::opAdd64ri8_},
          {"ADD32ri", &X86IGNILLifter::opAdd32ri_},
          {"ADD32ri8", &X86IGNILLifter::opAdd32ri8_},
          {"ADD64mr", &X86IGNILLifter::opAdd64mr_},
          {"ADD32mr", &X86IGNILLifter::opAdd32mr_},
          {"ADD64mi32", &X86IGNILLifter::opAdd64mi_},
          {"ADD64mi8", &X86IGNILLifter::opAdd64mi8_},
          {"ADD32mi", &X86IGNILLifter::opAdd32mi_},
          {"ADD32mi8", &X86IGNILLifter::opAdd32mi8_},
          {"ADD64rm", &X86IGNILLifter::opAdd64rm_},
          {"ADD32rm", &X86IGNILLifter::opAdd32rm_},
          {"SUB64rr", &X86IGNILLifter::opSub64rr_},
          {"SUB32rr", &X86IGNILLifter::opSub32rr_},
          {"SUB64ri32", &X86IGNILLifter::opSub64ri_},
          {"SUB64ri8", &X86IGNILLifter::opSub64ri8_},
          {"SUB32ri", &X86IGNILLifter::opSub32ri_},
          {"SUB32ri8", &X86IGNILLifter::opSub32ri8_},
          {"SUB64mr", &X86IGNILLifter::opSub64mr_},
          {"SUB32mr", &X86IGNILLifter::opSub32mr_},
          {"SUB64mi32", &X86IGNILLifter::opSub64mi_},
          {"SUB64mi8", &X86IGNILLifter::opSub64mi8_},
          {"SUB32mi", &X86IGNILLifter::opSub32mi_},
          {"SUB32mi8", &X86IGNILLifter::opSub32mi8_},
          {"SUB64rm", &X86IGNILLifter::opSub64rm_},
          {"SUB32rm", &X86IGNILLifter::opSub32rm_},
          {"XOR64rr", &X86IGNILLifter::opXor64rr_},
          {"XOR32rr", &X86IGNILLifter::opXor32rr_},
          {"XOR64ri32", &X86IGNILLifter::opXor64ri_},
          {"XOR64ri8", &X86IGNILLifter::opXor64ri8_},
          {"XOR32ri", &X86IGNILLifter::opXor32ri_},
          {"XOR32ri8", &X86IGNILLifter::opXor32ri8_},
          {"AND64rr", &X86IGNILLifter::opAnd64rr_},
          {"AND32rr", &X86IGNILLifter::opAnd32rr_},
          {"AND64ri32", &X86IGNILLifter::opAnd64ri_},
          {"AND64ri8", &X86IGNILLifter::opAnd64ri8_},
          {"AND32ri", &X86IGNILLifter::opAnd32ri_},
          {"AND32ri8", &X86IGNILLifter::opAnd32ri8_},
          {"OR64rr", &X86IGNILLifter::opOr64rr_},
          {"OR32rr", &X86IGNILLifter::opOr32rr_},
          {"OR64ri32", &X86IGNILLifter::opOr64ri_},
          {"OR64ri8", &X86IGNILLifter::opOr64ri8_},
          {"OR32ri", &X86IGNILLifter::opOr32ri_},
          {"OR32ri8", &X86IGNILLifter::opOr32ri8_},
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
          {"CMP64rm", &X86IGNILLifter::opCmp64rm_},
          {"CMP32rm", &X86IGNILLifter::opCmp32rm_},
          {"CMP64mr", &X86IGNILLifter::opCmp64mr_},
          {"CMP32mr", &X86IGNILLifter::opCmp32mr_},
          {"CMP64mi32", &X86IGNILLifter::opCmp64mi_},
          {"CMP64mi8", &X86IGNILLifter::opCmp64mi8_},
          {"CMP32mi", &X86IGNILLifter::opCmp32mi_},
          {"CMP32mi8", &X86IGNILLifter::opCmp32mi8_},
          {"TEST64rr", &X86IGNILLifter::opTest64rr_},
          {"TEST32rr", &X86IGNILLifter::opTest32rr_},
          {"TEST64ri32", &X86IGNILLifter::opTest64ri_},
          {"TEST32ri", &X86IGNILLifter::opTest32ri_},
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
          {"RETI64", &X86IGNILLifter::opRetI64_},
          {"RETIL", &X86IGNILLifter::opRetIL_},
          {"LEAVE64", &X86IGNILLifter::opLeave64_},
          {"LEAVE", &X86IGNILLifter::opLeave_},
          {"SAR32ri", &X86IGNILLifter::opSar32ri_},
          {"SAR64ri", &X86IGNILLifter::opSar64ri_},
          {"IMUL64rri32", &X86IGNILLifter::opImul64rri32_},
          {"IMUL64rri8", &X86IGNILLifter::opImul64rri8_},
          {"IMUL32rri", &X86IGNILLifter::opImul32rri_},
          {"IMUL32rri8", &X86IGNILLifter::opImul32rri8_},
          {"IMUL32rr", &X86IGNILLifter::opImul32rr_},
          {"IMUL64rr", &X86IGNILLifter::opImul64rr_},
          {"LEA64_32r", &X86IGNILLifter::opLea64_32r_},
          {"INC32r", &X86IGNILLifter::opInc32r_},
          {"INC64r", &X86IGNILLifter::opInc64r_},
          {"DEC32r", &X86IGNILLifter::opDec32r_},
          {"DEC64r", &X86IGNILLifter::opDec64r_},
          {"NEG32r", &X86IGNILLifter::opNeg32r_},
          {"NEG64r", &X86IGNILLifter::opNeg64r_},
          {"SHL32ri", &X86IGNILLifter::opShl32ri_},
          {"SHL64ri", &X86IGNILLifter::opShl64ri_},
          {"SHR32ri", &X86IGNILLifter::opShr32ri_},
          {"SHR64ri", &X86IGNILLifter::opShr64ri_},
          {"SAR32r1", &X86IGNILLifter::opSar32r1_},
          {"SAR64r1", &X86IGNILLifter::opSar64r1_},
          {"SHL32r1", &X86IGNILLifter::opShl32r1_},
          {"SHL64r1", &X86IGNILLifter::opShl64r1_},
          {"SHR32r1", &X86IGNILLifter::opShr32r1_},
          {"SHR64r1", &X86IGNILLifter::opShr64r1_},
          {"PXORrr", &X86IGNILLifter::opPxorRR_},
          {"CVTSI2SDrr_Int", &X86IGNILLifter::opCvtsi2sd32_},
          {"CVTSI642SDrr_Int", &X86IGNILLifter::opCvtsi2sd64_},
          {"CVTTSD2SIrr_Int", &X86IGNILLifter::opCvttsd2si32_},
          {"CVTTSD2SI64rr_Int", &X86IGNILLifter::opCvttsd2si64_},
          {"ADDSDrr_Int", &X86IGNILLifter::opAddsd_},
          {"SUBSDrr_Int", &X86IGNILLifter::opSubsd_},
          {"MULSDrr_Int", &X86IGNILLifter::opMulsd_},
          {"MULSDrm_Int", &X86IGNILLifter::opMulsdMem_},
          {"MOVSDrr", &X86IGNILLifter::opMovsdRR_},
          {"MOVSDrm", &X86IGNILLifter::opMovsdLoad_},
          {"MOVSDmr", &X86IGNILLifter::opMovsdStore_},
  };

  // MOVZX/MOVSX are kept out of kOpcodeFields: there are twenty-odd spellings
  // between them, all dispatching to one handler, and each needs the width of
  // the value it extends. A name -> width table keyed by opcode says both
  // things at once, where twenty named members would say neither.
  //
  // The width is the *source* width from the mnemonic (MOVZX32rm8 loads one
  // byte into a 32-bit register). For the memory-source forms it is the only
  // place that width exists: an x86 memory reference carries no size. The
  // rr/rm spelling is equally load-bearing - op[1] is a register in both
  // forms (the memory operand's base), so the name is the only thing that
  // distinguishes them.
  static const std::vector<std::pair<std::string_view, ignil::Width>>
          kMovzxSpellings = {
                  {"MOVZX16rr8", 8},        {"MOVZX16rm8", 8},
                  {"MOVZX16rr16", 16},      {"MOVZX16rm16", 16},
                  {"MOVZX32rr8", 8},        {"MOVZX32rm8", 8},
                  {"MOVZX32rr8_NOREX", 8},  {"MOVZX32rm8_NOREX", 8},
                  {"MOVZX32rr16", 16},      {"MOVZX32rm16", 16},
                  {"MOVZX64rr8", 8},        {"MOVZX64rm8", 8},
                  {"MOVZX64rr16", 16},      {"MOVZX64rm16", 16},
          };
  static const std::vector<std::pair<std::string_view, ignil::Width>>
          kMovsxSpellings = {
                  {"MOVSX16rr8", 8},        {"MOVSX16rm8", 8},
                  {"MOVSX16rr16", 16},      {"MOVSX16rm16", 16},
                  {"MOVSX16rr32", 32},      {"MOVSX16rm32", 32},
                  {"MOVSX32rr8", 8},        {"MOVSX32rm8", 8},
                  {"MOVSX32rr8_NOREX", 8},  {"MOVSX32rm8_NOREX", 8},
                  {"MOVSX32rr16", 16},      {"MOVSX32rm16", 16},
                  {"MOVSX32rr32", 32},      {"MOVSX32rm32", 32},
                  {"MOVSX64rr8", 8},        {"MOVSX64rm8", 8},
                  {"MOVSX64rr16", 16},      {"MOVSX64rm16", 16},
                  {"MOVSX64rr32", 32},      {"MOVSX64rm32", 32},
          };

  // Index them by name, tagging each with the operand form its spelling
  // implies. Every entry above is either ...rr<N> or ...rm<N>.
  const auto indexSpellings =
          [](const std::vector<std::pair<std::string_view, ignil::Width>> &in) {
            std::unordered_map<std::string_view, MovExtendForm> out;
            for (const auto &[name, width]: in)
              out.emplace(name, MovExtendForm{
                                        width,
                                        name.find("rm") != std::string_view::npos});
            return out;
          };
  const auto movzxByName = indexSpellings(kMovzxSpellings);
  const auto movsxByName = indexSpellings(kMovsxSpellings);

  for (unsigned i = 0; i < instrInfo.getNumOpcodes(); ++i) {
    const std::string_view name = instrInfo.getName(i);

    if (auto it = kOpcodeFields.find(name); it != kOpcodeFields.end())
      this->*(it->second) = i;
    else if (auto zx = movzxByName.find(name); zx != movzxByName.end())
      movzxForms_.emplace(i, zx->second);
    else if (auto sx = movsxByName.find(name); sx != movsxByName.end())
      movsxForms_.emplace(i, sx->second);
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

  // A block that ends without an explicit branch/return (execution simply
  // falls into the next block, e.g. an if/else arm with no trailing jump)
  // gets no JMP op from liftInstruction, since there is no branch
  // instruction to lift one from. Without a materialized terminator here,
  // LLVMLifter has no way to know where control flow goes next and emits a
  // basic block with no terminator at all - invalid LLVM IR that hangs
  // PromotePass's dominator-tree computation. Materialize the fall-through
  // edge explicitly using the CFG successor the disassembler already
  // computed.
  const bool alreadyTerminated =
          !result.empty() &&
          (result.ops().back().opcode == ignil::Opcode::JMP ||
           result.ops().back().opcode == ignil::Opcode::CJMP ||
           result.ops().back().opcode == ignil::Opcode::RET);
  if (!alreadyTerminated) {
    if (const auto &succ = block.successors(); succ.size() == 1) {
      result.append({ignil::Opcode::JMP,
                     std::nullopt,
                     {ignil::Const{*succ.begin(), 64}},
                     block.terminatorAddress()});
    }
  }

  return result;
}

dragon::ignil::Block dragon::x86::X86IGNILLifter::liftTrace(
        std::span<const Instruction> trace) const {
  if (trace.empty())
    return ignil::Block{0};

  ignil::Block result{trace.front().address()};
  std::uint32_t tempId = 0;

  for (const Instruction &insn: trace)
    liftInstruction(insn, result, tempId);

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
  else if (opc == opMov64mr_ || opc == opMov32mr_ || opc == opMov64mi_ ||
           opc == opMov32mi_)
    liftMovStore(*mc, addr, out, tempId);
  else if (opc == opAdd64rr_ || opc == opAdd32rr_ || opc == opAdd64ri_ ||
           opc == opAdd32ri_ || opc == opAdd64ri8_ || opc == opAdd32ri8_)
    liftAdd(*mc, addr, out, tempId);
  else if (opc == opAdd64mr_ || opc == opAdd32mr_ || opc == opAdd64mi_ ||
           opc == opAdd32mi_ || opc == opAdd64mi8_ || opc == opAdd32mi8_)
    liftAddMem(*mc, addr, out, tempId);
  else if (opc == opAdd64rm_ || opc == opAdd32rm_)
    liftAddRegMem(*mc, addr, out, tempId);
  else if (opc == opSub64rr_ || opc == opSub32rr_ || opc == opSub64ri_ ||
           opc == opSub32ri_ || opc == opSub64ri8_ || opc == opSub32ri8_)
    liftSub(*mc, addr, out, tempId);
  else if (opc == opSub64mr_ || opc == opSub32mr_ || opc == opSub64mi_ ||
           opc == opSub32mi_ || opc == opSub64mi8_ || opc == opSub32mi8_)
    liftSubMem(*mc, addr, out, tempId);
  else if (opc == opSub64rm_ || opc == opSub32rm_)
    liftSubRegMem(*mc, addr, out, tempId);
  else if (opc == opXor64rr_ || opc == opXor32rr_ || opc == opXor64ri_ ||
           opc == opXor32ri_ || opc == opXor64ri8_ || opc == opXor32ri8_)
    liftXor(*mc, addr, out, tempId);
  else if (opc == opAnd64rr_ || opc == opAnd32rr_ || opc == opAnd64ri_ ||
           opc == opAnd32ri_ || opc == opAnd64ri8_ || opc == opAnd32ri8_)
    liftAnd(*mc, addr, out, tempId);
  else if (opc == opOr64rr_ || opc == opOr32rr_ || opc == opOr64ri_ ||
           opc == opOr32ri_ || opc == opOr64ri8_ || opc == opOr32ri8_)
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
  else if (opc == opCmp64rm_ || opc == opCmp32rm_)
    liftCmpRegMem(*mc, addr, out, tempId);
  else if (opc == opCmp64mr_ || opc == opCmp32mr_ || opc == opCmp64mi_ ||
           opc == opCmp32mi_ || opc == opCmp64mi8_ || opc == opCmp32mi8_)
    liftCmpMem(*mc, addr, out, tempId);
  else if (opc == opTest64rr_ || opc == opTest32rr_ || opc == opTest64ri_ ||
           opc == opTest32ri_)
    liftTest(*mc, addr, out, tempId);
  else if (opc == opCall_ || opc == opCall64r_)
    liftCall(*mc, addr, insn.size(), out, tempId);
  else if (opc == opRet64_ || opc == opRetL_ || opc == opRetI64_ ||
           opc == opRetIL_)
    liftRet(*mc, addr, out, tempId);
  else if (opc == opLeave64_ || opc == opLeave_)
    liftLeave(*mc, addr, out, tempId);
  else if (opc == opJmp64m_ || opc == opJmp32m_)
    liftJmpMem(*mc, addr, insn.size(), out, tempId);
  else if (opc == opCall64m_ || opc == opCall32m_)
    liftCallMem(*mc, addr, insn.size(), out, tempId);
  else if (auto zx = movzxForms_.find(opc); zx != movzxForms_.end())
    liftMovExtend(*mc, addr, zx->second, /*signExtend=*/false, out, tempId);
  else if (auto sx = movsxForms_.find(opc); sx != movsxForms_.end())
    liftMovExtend(*mc, addr, sx->second, /*signExtend=*/true, out, tempId);
  else if (opc == opSar32ri_ || opc == opSar64ri_)
    liftSarImm(*mc, addr, out, tempId);
  else if (opc == opImul64rri32_ || opc == opImul64rri8_ ||
           opc == opImul32rri_ || opc == opImul32rri8_)
    liftImulImm(*mc, addr, out, tempId);
  else if (opc == opImul32rr_ || opc == opImul64rr_)
    liftImulRR(*mc, addr, out, tempId);
  else if (opc == opLea64_32r_)
    liftLeaTrunc32(*mc, addr, out, tempId);
  else if (opc == opInc32r_ || opc == opInc64r_)
    liftInc(*mc, addr, out, tempId);
  else if (opc == opDec32r_ || opc == opDec64r_)
    liftDec(*mc, addr, out, tempId);
  else if (opc == opNeg32r_ || opc == opNeg64r_)
    liftNeg(*mc, addr, out, tempId);
  else if (opc == opShl32ri_ || opc == opShl64ri_)
    liftShlImm(*mc, addr, out, tempId);
  else if (opc == opShr32ri_ || opc == opShr64ri_)
    liftShrImm(*mc, addr, out, tempId);
  else if (opc == opSar32r1_ || opc == opSar64r1_)
    liftSarR1(*mc, addr, out, tempId);
  else if (opc == opShl32r1_ || opc == opShl64r1_)
    liftShlR1(*mc, addr, out, tempId);
  else if (opc == opShr32r1_ || opc == opShr64r1_)
    liftShrR1(*mc, addr, out, tempId);
  else if (opc == opPxorRR_)
    liftXor(*mc, addr, out, tempId);
  else if (opc == opCvtsi2sd32_ || opc == opCvtsi2sd64_)
    liftCvtsi2sd(*mc, addr, out, tempId);
  else if (opc == opCvttsd2si32_ || opc == opCvttsd2si64_)
    liftCvttsd2si(*mc, addr, out, tempId);
  else if (opc == opAddsd_)
    liftAddsd(*mc, addr, out, tempId);
  else if (opc == opSubsd_)
    liftSubsd(*mc, addr, out, tempId);
  else if (opc == opMulsd_)
    liftMulsd(*mc, addr, out, tempId);
  else if (opc == opMulsdMem_)
    liftMulsdMem(*mc, addr, out, tempId);
  else if (opc == opMovsdRR_)
    liftMovsdRR(*mc, addr, out, tempId);
  else if (opc == opMovsdLoad_)
    liftMovLoad(*mc, addr, out, tempId);
  else if (opc == opMovsdStore_)
    liftMovStore(*mc, addr, out, tempId);
  else {
    const auto &desc = instrInfo_->get(opc);
    if (desc.isConditionalBranch()) {
      liftJcc(*mc, addr, insn.size(), out, tempId);
    } else if (desc.isBranch()) {
      liftJmp(*mc, addr, insn.size(), out, tempId);
    } else {
      // Unknown instruction. Side-effect-free ones (prefetch, fence, nop,
      // ...) have no explicit register-def operands, so nothing is emitted
      // for them - preserves the existing "no UNDEF for no-op instructions"
      // behavior. Anything that *does* define a register but has no lifter
      // yet must still mark that register UNDEF, so downstream flag/value
      // reads see an honest "unknown" instead of a silently stale value
      // (this is what let a dropped mem-destination MOV/ADD/SUB/CMP corrupt
      // later flag reads before the dedicated lifters above existed).
      for (unsigned i = 0; i < desc.getNumDefs(); ++i) {
        const auto &defOp = mc->getOperand(i);
        if (!defOp.isReg())
          continue;
        if (auto dst = toReg(defOp.getReg()))
          emitUndef(*dst, addr, out);
      }
    }
  }
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
  // op[0..4]=mem(base,scale,index,disp,seg), op[5]=src (reg, e.g. MOV64mr /
  // MOV32mr) or imm (e.g. MOV64mi32 / MOV32mi - `mov dword ptr [x], 0`).
  ignil::Width ptrWidth = (architecture_ == Architecture::X86_64) ? 64 : 32;
  const auto &srcOp = mc.getOperand(5);

  ignil::Value src;
  if (srcOp.isImm()) {
    // The memory operand carries no register to read a width from; the
    // opcode variant (32 vs 64) is the only source of truth for how wide
    // the store is.
    const ignil::Width opWidth = (mc.getOpcode() == opMov64mi_) ? 64 : 32;
    src = ignil::Const{static_cast<std::uint64_t>(srcOp.getImm()), opWidth};
  } else {
    auto srcReg = toReg(srcOp.getReg());
    if (!srcReg) {
      logger_.log("Error liftMovStore in addr {:#x} no src", addr);
      return;
    }
    src = *srcReg;
  }

  auto addrTemp = emitAddress(mc, 0, ptrWidth, addr, out, tmp);
  out.append({ignil::Opcode::STORE, std::nullopt, {addrTemp, src}, addr});
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
void dragon::x86::X86IGNILLifter::liftAddMem(const llvm::MCInst &mc,
                                             std::uint64_t addr,
                                             ignil::Block &out,
                                             std::uint32_t &tmp) const {
  // RMW: op[0..4]=mem(base,scale,index,disp,seg), op[5]=reg-or-imm addend.
  // The destination is memory itself: load the current value, add, store
  // the result back, then set flags exactly like the register form.
  const ignil::Width ptrWidth =
          (architecture_ == Architecture::X86_64) ? 64 : 32;
  const unsigned opc = mc.getOpcode();
  const ignil::Width opWidth =
          (opc == opAdd64mr_ || opc == opAdd64mi_ || opc == opAdd64mi8_) ? 64
                                                                          : 32;

  ignil::Value addend;
  const auto &srcOp = mc.getOperand(5);
  if (srcOp.isImm()) {
    addend = ignil::Const{static_cast<std::uint64_t>(srcOp.getImm()),
                          opWidth};
  } else {
    auto srcReg = toReg(srcOp.getReg());
    if (!srcReg) {
      logger_.log("Error liftAddMem in addr {:#x} no src", addr);
      return;
    }
    addend = *srcReg;
  }

  auto addrTemp = emitAddress(mc, 0, ptrWidth, addr, out, tmp);
  auto lhs = newTemp(tmp, opWidth);
  out.append({ignil::Opcode::LOAD, lhs, {addrTemp}, addr});

  auto result = newTemp(tmp, opWidth);
  out.append({ignil::Opcode::ADD, result, {lhs, addend}, addr});
  out.append({ignil::Opcode::STORE, std::nullopt, {addrTemp, result}, addr});
  emitArithFlags(addr, out, tmp,
                 {
                         {X86Flag::CF, cfAddOverflow(result, addend, addr)},
                         {X86Flag::ZF, zfEqZero(result, addr)},
                         {X86Flag::SF, sfSignBit(result, addr)},
                         {X86Flag::OF, ofAddOverflow(lhs, addend, result, addr)},
                 });
}

void dragon::x86::X86IGNILLifter::liftSubMem(const llvm::MCInst &mc,
                                             std::uint64_t addr,
                                             ignil::Block &out,
                                             std::uint32_t &tmp) const {
  // RMW: op[0..4]=mem(base,scale,index,disp,seg), op[5]=reg-or-imm
  // subtrahend. Mirrors liftAddMem but with SUB semantics/flags.
  const ignil::Width ptrWidth =
          (architecture_ == Architecture::X86_64) ? 64 : 32;
  const unsigned opc = mc.getOpcode();
  const ignil::Width opWidth =
          (opc == opSub64mr_ || opc == opSub64mi_ || opc == opSub64mi8_) ? 64
                                                                          : 32;

  ignil::Value rhs;
  const auto &srcOp = mc.getOperand(5);
  if (srcOp.isImm()) {
    rhs = ignil::Const{static_cast<std::uint64_t>(srcOp.getImm()), opWidth};
  } else {
    auto srcReg = toReg(srcOp.getReg());
    if (!srcReg) {
      logger_.log("Error liftSubMem in addr {:#x} no src", addr);
      return;
    }
    rhs = *srcReg;
  }

  auto addrTemp = emitAddress(mc, 0, ptrWidth, addr, out, tmp);
  auto lhs = newTemp(tmp, opWidth);
  out.append({ignil::Opcode::LOAD, lhs, {addrTemp}, addr});

  auto result = newTemp(tmp, opWidth);
  out.append({ignil::Opcode::SUB, result, {lhs, rhs}, addr});
  out.append({ignil::Opcode::STORE, std::nullopt, {addrTemp, result}, addr});
  emitArithFlags(addr, out, tmp,
                 {
                         {X86Flag::CF, cfSubBorrow(lhs, rhs, addr)},
                         {X86Flag::ZF, zfEqZero(result, addr)},
                         {X86Flag::SF, sfSignBit(result, addr)},
                         {X86Flag::OF, ofSubOverflow(lhs, rhs, result, addr)},
                 });
}

void dragon::x86::X86IGNILLifter::liftAddRegMem(const llvm::MCInst &mc,
                                                std::uint64_t addr,
                                                ignil::Block &out,
                                                std::uint32_t &tmp) const {
  // op[0]=dst(out), op[1]=src1(tied=dst), op[2..6]=mem(rhs). Load rhs from
  // memory, then the same ADD + flags as the register form (liftAdd).
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftAddRegMem in addr {:#x} no dst", addr);
    return;
  }

  const ignil::Width ptrWidth =
          (architecture_ == Architecture::X86_64) ? 64 : 32;
  auto addrTemp = emitAddress(mc, 2, ptrWidth, addr, out, tmp);
  auto src = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::LOAD, src, {addrTemp}, addr});

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

void dragon::x86::X86IGNILLifter::liftSubRegMem(const llvm::MCInst &mc,
                                                std::uint64_t addr,
                                                ignil::Block &out,
                                                std::uint32_t &tmp) const {
  // Mirrors liftAddRegMem but with SUB semantics/flags.
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftSubRegMem in addr {:#x} no dst", addr);
    return;
  }

  const ignil::Width ptrWidth =
          (architecture_ == Architecture::X86_64) ? 64 : 32;
  auto addrTemp = emitAddress(mc, 2, ptrWidth, addr, out, tmp);
  auto rhs = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::LOAD, rhs, {addrTemp}, addr});

  auto lhs = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::COPY, lhs, {*dst}, addr});

  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::SUB, result, {lhs, rhs}, addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});
  emitArithFlags(addr, out, tmp,
                 {
                         {X86Flag::CF, cfSubBorrow(lhs, rhs, addr)},
                         {X86Flag::ZF, zfEqZero(result, addr)},
                         {X86Flag::SF, sfSignBit(result, addr)},
                         {X86Flag::OF, ofSubOverflow(lhs, rhs, result, addr)},
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
  // op[0]=dst(out), op[1]=src1(tied=dst), op[2]=src2(reg or imm)
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftXor in addr {:#x} no dst", addr);
    return;
  }

  ignil::Value src;
  const auto &srcOp = mc.getOperand(2);
  if (srcOp.isImm()) {
    src = ignil::Const{static_cast<std::uint64_t>(srcOp.getImm()), dst->width};
  } else {
    auto srcReg = toReg(srcOp.getReg());
    if (!srcReg) {
      logger_.log("Error liftXor in addr {:#x} no src", addr);
      emitUndef(*dst, addr, out);
      return;
    }
    src = *srcReg;
  }
  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::XOR, result, {*dst, src}, addr});
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
  // op[0]=dst(out), op[1]=src1(tied=dst), op[2]=src2(reg or imm)
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftAnd in addr {:#x} no dst", addr);
    return;
  }

  ignil::Value src;
  const auto &srcOp = mc.getOperand(2);
  if (srcOp.isImm()) {
    src = ignil::Const{static_cast<std::uint64_t>(srcOp.getImm()), dst->width};
  } else {
    auto srcReg = toReg(srcOp.getReg());
    if (!srcReg) {
      logger_.log("Error liftAnd in addr {:#x} no src", addr);
      emitUndef(*dst, addr, out);
      return;
    }
    src = *srcReg;
  }
  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::AND, result, {*dst, src}, addr});
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
  // op[0]=dst(out), op[1]=src1(tied=dst), op[2]=src2(reg or imm)
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftOr in addr {:#x} no dst", addr);
    return;
  }

  ignil::Value src;
  const auto &srcOp = mc.getOperand(2);
  if (srcOp.isImm()) {
    src = ignil::Const{static_cast<std::uint64_t>(srcOp.getImm()), dst->width};
  } else {
    auto srcReg = toReg(srcOp.getReg());
    if (!srcReg) {
      logger_.log("Error liftOr in addr {:#x} no src", addr);
      emitUndef(*dst, addr, out);
      return;
    }
    src = *srcReg;
  }
  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::OR, result, {*dst, src}, addr});
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
void dragon::x86::X86IGNILLifter::liftCmpRegMem(const llvm::MCInst &mc,
                                                std::uint64_t addr,
                                                ignil::Block &out,
                                                std::uint32_t &tmp) const {
  // CMP reg,mem: op[0]=reg(lhs), op[1..5]=mem(rhs). Load rhs, SUB, discard
  // the result but keep the flags - same as liftCmp with rhs from memory.
  auto lhs = toReg(mc.getOperand(0).getReg());
  if (!lhs) {
    logger_.log("Error liftCmpRegMem in addr {:#x} no lhs", addr);
    return;
  }

  const ignil::Width ptrWidth =
          (architecture_ == Architecture::X86_64) ? 64 : 32;
  auto addrTemp = emitAddress(mc, 1, ptrWidth, addr, out, tmp);
  auto rhs = newTemp(tmp, lhs->width);
  out.append({ignil::Opcode::LOAD, rhs, {addrTemp}, addr});

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

void dragon::x86::X86IGNILLifter::liftCmpMem(const llvm::MCInst &mc,
                                             std::uint64_t addr,
                                             ignil::Block &out,
                                             std::uint32_t &tmp) const {
  // CMP mem,reg / CMP mem,imm: op[0..4]=mem(lhs), op[5]=reg-or-imm(rhs).
  // Load lhs, SUB, discard the result but keep the flags. No writeback -
  // unlike ADD/SUB, CMP never modifies its destination.
  const ignil::Width ptrWidth =
          (architecture_ == Architecture::X86_64) ? 64 : 32;
  const unsigned opc = mc.getOpcode();
  const ignil::Width opWidth =
          (opc == opCmp64mr_ || opc == opCmp64mi_ || opc == opCmp64mi8_) ? 64
                                                                          : 32;

  ignil::Value rhs;
  const auto &srcOp = mc.getOperand(5);
  if (srcOp.isImm()) {
    rhs = ignil::Const{static_cast<std::uint64_t>(srcOp.getImm()), opWidth};
  } else {
    auto srcReg = toReg(srcOp.getReg());
    if (!srcReg) {
      logger_.log("Error liftCmpMem in addr {:#x} no rhs", addr);
      return;
    }
    rhs = *srcReg;
  }

  auto addrTemp = emitAddress(mc, 0, ptrWidth, addr, out, tmp);
  auto lhs = newTemp(tmp, opWidth);
  out.append({ignil::Opcode::LOAD, lhs, {addrTemp}, addr});

  auto result = newTemp(tmp, opWidth);
  out.append({ignil::Opcode::SUB, result, {lhs, rhs}, addr});
  emitArithFlags(addr, out, tmp,
                 {
                         {X86Flag::CF, cfSubBorrow(lhs, rhs, addr)},
                         {X86Flag::ZF, zfEqZero(result, addr)},
                         {X86Flag::SF, sfSignBit(result, addr)},
                         {X86Flag::OF, ofSubOverflow(lhs, rhs, result, addr)},
                 });
}

void dragon::x86::X86IGNILLifter::liftTest(const llvm::MCInst &mc,
                                           std::uint64_t addr,
                                           ignil::Block &out,
                                           std::uint32_t &tmp) const {
  // op[0]=lhs(reg), op[1]=rhs(reg or imm)
  auto lhs = toReg(mc.getOperand(0).getReg());
  if (!lhs) {
    logger_.log("Error liftTest in addr {:#x} no lhs", addr);
    return;
  }

  ignil::Value rhs;
  const auto &rhsOp = mc.getOperand(1);
  if (rhsOp.isImm()) {
    rhs = ignil::Const{static_cast<std::uint64_t>(rhsOp.getImm()), lhs->width};
  } else {
    auto rhsReg = toReg(rhsOp.getReg());
    if (!rhsReg) {
      logger_.log("Error liftTest in addr {:#x} no rhs", addr);
      return;
    }
    rhs = *rhsReg;
  }

  // TEST is AND that discards the result but keeps flags; CF and OF always 0.
  auto result = newTemp(tmp, lhs->width);
  out.append({ignil::Opcode::AND, result, {*lhs, rhs}, addr});
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
  // ret N (RETI64/RETIL) pops N extra bytes after the return address, but
  // control transfers before that cleanup is visible to the callee, so a
  // plain RET is sufficient for intra-function analysis.
  out.append({ignil::Opcode::RET, std::nullopt, {}, addr});
}

void dragon::x86::X86IGNILLifter::liftLeave(const llvm::MCInst &mc,
                                            std::uint64_t addr,
                                            ignil::Block &out,
                                            std::uint32_t &tmp) const {
  // leave ≡  mov rsp, rbp  ;  pop rbp
  // Decomposed into IGNIL primitives (P-CODE / VEX style):
  //   rsp  = rbp
  //   t0   = memory[rsp]
  //   rbp  = t0
  //   rsp  = rsp + ptrBytes
  const bool is64 = (architecture_ == Architecture::X86_64);
  const ignil::Width ptrWidth = is64 ? 64 : 32;
  const std::uint64_t ptrBytes = is64 ? 8 : 4;
  ignil::Reg sp = is64 ? regRSP_ : regESP_;
  ignil::Reg bp = is64 ? regRBP_ : regEBP_;

  // mov rsp, rbp
  out.append({ignil::Opcode::COPY, sp, {bp}, addr});

  // pop rbp
  auto loaded = newTemp(tmp, bp.width);
  out.append({ignil::Opcode::LOAD, loaded, {sp}, addr});
  out.append({ignil::Opcode::COPY, bp, {loaded}, addr});
  auto newSP = newTemp(tmp, ptrWidth);
  out.append({ignil::Opcode::ADD,
              newSP,
              {sp, ignil::Const{ptrBytes, ptrWidth}},
              addr});
  out.append({ignil::Opcode::COPY, sp, {newSP}, addr});
}

void dragon::x86::X86IGNILLifter::liftMovExtend(const llvm::MCInst &mc,
                                               std::uint64_t addr,
                                               MovExtendForm form,
                                               bool signExtend,
                                               ignil::Block &out,
                                               std::uint32_t &tmp) const {
  // op[0]=dst(wider); op[1]=src(narrower) for the rr forms, op[1..5]=mem for
  // the rm ones. Neither MOVSX nor MOVZX affects flags.
  const ignil::Opcode extend =
          signExtend ? ignil::Opcode::SEXT : ignil::Opcode::ZEXT;
  const char *what = signExtend ? "liftMovsx" : "liftMovzx";

  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error {} in addr {:#x} no dst", what, addr);
    return;
  }

  // Memory source: load form.srcWidth bits from the effective address, then
  // widen. Note this cannot be decided from the operands - op[1] is a
  // register in both forms (here it is the memory operand's base), so the
  // opcode's own spelling is what says which it is.
  if (form.memorySource) {
    const ignil::Width ptrWidth =
            (architecture_ == Architecture::X86_64) ? 64 : 32;
    auto addrTemp = emitAddress(mc, 1, ptrWidth, addr, out, tmp);
    auto loaded = newTemp(tmp, form.srcWidth);
    out.append({ignil::Opcode::LOAD, loaded, {addrTemp}, addr});
    out.append({extend, *dst, {loaded}, addr});
    return;
  }

  auto src = toReg(mc.getOperand(1).getReg());
  if (!src) {
    logger_.log("Error {} in addr {:#x} no src", what, addr);
    emitUndef(*dst, addr, out);
    return;
  }
  out.append({extend, *dst, {*src}, addr});
}

void dragon::x86::X86IGNILLifter::liftSarImm(const llvm::MCInst &mc,
                                             std::uint64_t addr,
                                             ignil::Block &out,
                                             std::uint32_t &tmp) const {
  // op[0]=dst(out), op[1]=src(tied=dst), op[2]=imm shift count.
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftSarImm in addr {:#x} no dst", addr);
    return;
  }
  auto src = toReg(mc.getOperand(1).getReg());
  if (!src) {
    logger_.log("Error liftSarImm in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }
  const auto imm = static_cast<std::uint64_t>(mc.getOperand(2).getImm());

  // Real x86: shift by 0 leaves every flag - and the value - exactly as it
  // was, not even recomputed.
  if (imm == 0) {
    out.append({ignil::Opcode::COPY, *dst, {*src}, addr});
    return;
  }

  // Save the original value before dst gets overwritten below (dst and src
  // are the same tied register) - the CF FlagSetter below runs later, after
  // that overwrite, so it must read this saved Temp, not the Reg directly
  // (same reasoning liftAdd's `lhs` save applies to OF).
  auto lhs = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::COPY, lhs, {*src}, addr});

  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::SAR,
              result,
              {ignil::Value{lhs}, ignil::Const{imm, dst->width}},
              addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});

  auto cfLastBitOut = [this, lhs, imm,
                       addr](ignil::Block &out, std::uint32_t &tmp) {
    const ignil::Width w = lhs.width;
    auto shifted = newTemp(tmp, w);
    out.append({ignil::Opcode::SHR,
                shifted,
                {ignil::Value{lhs}, ignil::Const{imm - 1, w}},
                addr});
    auto masked = newTemp(tmp, w);
    out.append(
            {ignil::Opcode::AND, masked, {shifted, ignil::Const{1, w}}, addr});
    auto bit = newTemp(tmp, 1);
    out.append(
            {ignil::Opcode::NEQ, bit, {masked, ignil::Const{0, w}}, addr});
    out.append({ignil::Opcode::COPY, regCF_, {bit}, addr});
  };

  // OF is only architecturally defined for a 1-bit shift, where an
  // arithmetic right shift can never overflow (the sign bit is replicated,
  // never changed) - always 0. Left UNDEF (omitted from the map) for any
  // other count, matching this lifter's "UNDEF for genuinely
  // spec-undefined" convention elsewhere rather than guessing.
  std::map<X86Flag, FlagSetter> flags{
          {X86Flag::CF, cfLastBitOut},
          {X86Flag::ZF, zfEqZero(result, addr)},
          {X86Flag::SF, sfSignBit(result, addr)},
  };
  if (imm == 1)
    flags.emplace(X86Flag::OF, [this, addr](ignil::Block &out, std::uint32_t &) {
      out.append({ignil::Opcode::COPY, regOF_, {ignil::Const{0, 1}}, addr});
    });
  emitArithFlags(addr, out, tmp, flags);
}

void dragon::x86::X86IGNILLifter::liftImulImm(const llvm::MCInst &mc,
                                              std::uint64_t addr,
                                              ignil::Block &out,
                                              std::uint32_t &tmp) const {
  // op[0]=dst, op[1]=src, op[2]=imm (already sign-extended to dst's width
  // by LLVM's MCInst decode - same convention liftAdd already relies on).
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftImulImm in addr {:#x} no dst", addr);
    return;
  }
  auto src = toReg(mc.getOperand(1).getReg());
  if (!src) {
    logger_.log("Error liftImulImm in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }
  const auto imm = static_cast<std::uint64_t>(mc.getOperand(2).getImm());

  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::MUL,
              result,
              {*src, ignil::Const{imm, dst->width}},
              addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});

  // All flags UNDEF: SF/ZF/AF/PF are architecturally undefined for IMUL,
  // and CF/OF would need a full double-width product to detect overflow
  // correctly, which IGNIL's fixed-width model doesn't support - honestly
  // UNDEF rather than a guessed, likely-wrong formula.
  emitArithFlags(addr, out, tmp, {});
}

void dragon::x86::X86IGNILLifter::liftImulRR(const llvm::MCInst &mc,
                                             std::uint64_t addr,
                                             ignil::Block &out,
                                             std::uint32_t &tmp) const {
  // op[0]=dst(out), op[1]=src1(tied=dst), op[2]=src2(reg) - unlike
  // liftImulImm, both operands are registers here, so op[1] really is the
  // same register as op[0] (the tied 2-address form), and the actual second
  // factor is op[2].
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftImulRR in addr {:#x} no dst", addr);
    return;
  }
  auto src = toReg(mc.getOperand(2).getReg());
  if (!src) {
    logger_.log("Error liftImulRR in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }

  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::MUL, result, {*dst, *src}, addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});

  // Same all-UNDEF rationale as liftImulImm.
  emitArithFlags(addr, out, tmp, {});
}

void dragon::x86::X86IGNILLifter::liftLeaTrunc32(const llvm::MCInst &mc,
                                                 std::uint64_t addr,
                                                 ignil::Block &out,
                                                 std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftLeaTrunc32 in addr {:#x} no dst", addr);
    return;
  }
  // Address registers are always 64-bit for this form (that's the whole
  // point of LEA64_32r vs plain LEA32r), regardless of the 32-bit dst.
  auto addrTemp = emitAddress(mc, 1, 64, addr, out, tmp);
  auto truncated = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::TRUNC, truncated, {addrTemp}, addr});
  out.append({ignil::Opcode::COPY, *dst, {truncated}, addr});
}

void dragon::x86::X86IGNILLifter::liftInc(const llvm::MCInst &mc,
                                          std::uint64_t addr,
                                          ignil::Block &out,
                                          std::uint32_t &tmp) const {
  // op[0]=dst(out), op[1]=src(tied=dst).
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftInc in addr {:#x} no dst", addr);
    return;
  }

  // Save the original value before dst gets overwritten (same reasoning
  // as liftAdd's `lhs` and liftSarImm's fix earlier).
  auto lhs = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::COPY, lhs, {*dst}, addr});

  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::ADD,
              result,
              {ignil::Value{lhs}, ignil::Const{1, dst->width}},
              addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});

  // CF is deliberately NOT touched at all - no Op for it, not even UNDEF.
  // That is the entire point of INC over ADD-by-1: it must not disturb a
  // carry chain a previous instruction set up. PF/AF stay UNDEF, matching
  // this lifter's existing convention elsewhere.
  emitUndef(regPF_, addr, out);
  emitUndef(regAF_, addr, out);
  zfEqZero(result, addr)(out, tmp);
  sfSignBit(result, addr)(out, tmp);
  // OF: set iff the original value was the maximum positive value for its
  // width (non-negative before, negative after - a positive overflow).
  {
    auto lhsNeg = newTemp(tmp, 1);
    out.append(
            {ignil::Opcode::SLT, lhsNeg, {ignil::Value{lhs}, ignil::Const{0, dst->width}}, addr});
    auto lhsNonNeg = newTemp(tmp, 1);
    out.append({ignil::Opcode::NOT, lhsNonNeg, {ignil::Value{lhsNeg}}, addr});
    auto resNeg = newTemp(tmp, 1);
    out.append({ignil::Opcode::SLT, resNeg, {result, ignil::Const{0, dst->width}}, addr});
    auto of = newTemp(tmp, 1);
    out.append({ignil::Opcode::AND, of, {ignil::Value{lhsNonNeg}, ignil::Value{resNeg}}, addr});
    out.append({ignil::Opcode::COPY, regOF_, {ignil::Value{of}}, addr});
  }
  emitUndef(regDF_, addr, out);
}

void dragon::x86::X86IGNILLifter::liftDec(const llvm::MCInst &mc,
                                          std::uint64_t addr,
                                          ignil::Block &out,
                                          std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftDec in addr {:#x} no dst", addr);
    return;
  }

  auto lhs = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::COPY, lhs, {*dst}, addr});

  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::SUB,
              result,
              {ignil::Value{lhs}, ignil::Const{1, dst->width}},
              addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});

  // Same CF-untouched reasoning as liftInc.
  emitUndef(regPF_, addr, out);
  emitUndef(regAF_, addr, out);
  zfEqZero(result, addr)(out, tmp);
  sfSignBit(result, addr)(out, tmp);
  // OF: set iff the original value was the minimum negative value for its
  // width (negative before, non-negative after - a negative overflow).
  {
    auto lhsNeg = newTemp(tmp, 1);
    out.append(
            {ignil::Opcode::SLT, lhsNeg, {ignil::Value{lhs}, ignil::Const{0, dst->width}}, addr});
    auto resNeg = newTemp(tmp, 1);
    out.append({ignil::Opcode::SLT, resNeg, {result, ignil::Const{0, dst->width}}, addr});
    auto resNonNeg = newTemp(tmp, 1);
    out.append({ignil::Opcode::NOT, resNonNeg, {ignil::Value{resNeg}}, addr});
    auto of = newTemp(tmp, 1);
    out.append({ignil::Opcode::AND, of, {ignil::Value{lhsNeg}, ignil::Value{resNonNeg}}, addr});
    out.append({ignil::Opcode::COPY, regOF_, {ignil::Value{of}}, addr});
  }
  emitUndef(regDF_, addr, out);
}

void dragon::x86::X86IGNILLifter::liftNeg(const llvm::MCInst &mc,
                                          std::uint64_t addr,
                                          ignil::Block &out,
                                          std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftNeg in addr {:#x} no dst", addr);
    return;
  }

  auto lhs = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::COPY, lhs, {*dst}, addr});

  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::NEG, result, {ignil::Value{lhs}}, addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});

  std::map<X86Flag, FlagSetter> flags{
          // CF = (src != 0): negating zero doesn't borrow, anything else
          // does.
          {X86Flag::CF,
           [this, lhs, addr](ignil::Block &out, std::uint32_t &tmp) {
             auto cf = newTemp(tmp, 1);
             out.append({ignil::Opcode::NEQ,
                         cf,
                         {ignil::Value{lhs}, ignil::Const{0, lhs.width}},
                         addr});
             out.append({ignil::Opcode::COPY, regCF_, {ignil::Value{cf}}, addr});
           }},
          {X86Flag::ZF, zfEqZero(result, addr)},
          {X86Flag::SF, sfSignBit(result, addr)},
          // OF = (src was the minimum negative value for its width) - the
          // one value whose negation can't be represented.
          {X86Flag::OF,
           [this, lhs, addr](ignil::Block &out, std::uint32_t &tmp) {
             const std::uint64_t minNeg =
                     lhs.width >= 64 ? (std::uint64_t{1} << 63)
                                     : (std::uint64_t{1} << (lhs.width - 1));
             auto of = newTemp(tmp, 1);
             out.append({ignil::Opcode::EQ,
                         of,
                         {ignil::Value{lhs}, ignil::Const{minNeg, lhs.width}},
                         addr});
             out.append({ignil::Opcode::COPY, regOF_, {ignil::Value{of}}, addr});
           }},
  };
  emitArithFlags(addr, out, tmp, flags);
}


void dragon::x86::X86IGNILLifter::liftShlImm(const llvm::MCInst &mc,
                                             std::uint64_t addr,
                                             ignil::Block &out,
                                             std::uint32_t &tmp) const {
  // op[0]=dst(out), op[1]=src(tied=dst), op[2]=imm shift count.
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftShlImm in addr {:#x} no dst", addr);
    return;
  }
  auto src = toReg(mc.getOperand(1).getReg());
  if (!src) {
    logger_.log("Error liftShlImm in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }
  const auto imm = static_cast<std::uint64_t>(mc.getOperand(2).getImm());

  if (imm == 0) {
    out.append({ignil::Opcode::COPY, *dst, {*src}, addr});
    return;
  }

  auto lhs = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::COPY, lhs, {*src}, addr});

  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::SHL,
              result,
              {ignil::Value{lhs}, ignil::Const{imm, dst->width}},
              addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});

  // CF = the last bit shifted out the top (bit width-imm of the original
  // value) - the opposite end from SAR/SHR.
  auto cfLastBitOut = [this, lhs, imm,
                       addr](ignil::Block &out, std::uint32_t &tmp) {
    const ignil::Width w = lhs.width;
    auto shifted = newTemp(tmp, w);
    out.append({ignil::Opcode::SHR,
                shifted,
                {ignil::Value{lhs}, ignil::Const{w - imm, w}},
                addr});
    auto masked = newTemp(tmp, w);
    out.append(
            {ignil::Opcode::AND, masked, {shifted, ignil::Const{1, w}}, addr});
    auto bit = newTemp(tmp, 1);
    out.append(
            {ignil::Opcode::NEQ, bit, {masked, ignil::Const{0, w}}, addr});
    out.append({ignil::Opcode::COPY, regCF_, {bit}, addr});
  };

  std::map<X86Flag, FlagSetter> flags{
          {X86Flag::CF, cfLastBitOut},
          {X86Flag::ZF, zfEqZero(result, addr)},
          {X86Flag::SF, sfSignBit(result, addr)},
  };
  // OF only architecturally defined for a 1-bit shift: whether the sign
  // bit changed (SHL, unlike SAR/SHR, can overflow in either direction).
  if (imm == 1)
    flags.emplace(X86Flag::OF, [this, lhs, result,
                                addr](ignil::Block &out, std::uint32_t &tmp) {
      auto lhsNeg = newTemp(tmp, 1);
      out.append({ignil::Opcode::SLT,
                  lhsNeg,
                  {ignil::Value{lhs}, ignil::Const{0, lhs.width}},
                  addr});
      auto resNeg = newTemp(tmp, 1);
      out.append({ignil::Opcode::SLT,
                  resNeg,
                  {result, ignil::Const{0, ignil::widthOf(result)}},
                  addr});
      auto of = newTemp(tmp, 1);
      out.append({ignil::Opcode::NEQ, of, {lhsNeg, resNeg}, addr});
      out.append({ignil::Opcode::COPY, regOF_, {of}, addr});
    });
  emitArithFlags(addr, out, tmp, flags);
}

void dragon::x86::X86IGNILLifter::liftShrImm(const llvm::MCInst &mc,
                                             std::uint64_t addr,
                                             ignil::Block &out,
                                             std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftShrImm in addr {:#x} no dst", addr);
    return;
  }
  auto src = toReg(mc.getOperand(1).getReg());
  if (!src) {
    logger_.log("Error liftShrImm in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }
  const auto imm = static_cast<std::uint64_t>(mc.getOperand(2).getImm());

  if (imm == 0) {
    out.append({ignil::Opcode::COPY, *dst, {*src}, addr});
    return;
  }

  auto lhs = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::COPY, lhs, {*src}, addr});

  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::SHR,
              result,
              {ignil::Value{lhs}, ignil::Const{imm, dst->width}},
              addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});

  // CF = bit imm-1 of the original value - same formula shape as SAR.
  auto cfLastBitOut = [this, lhs, imm,
                       addr](ignil::Block &out, std::uint32_t &tmp) {
    const ignil::Width w = lhs.width;
    auto shifted = newTemp(tmp, w);
    out.append({ignil::Opcode::SHR,
                shifted,
                {ignil::Value{lhs}, ignil::Const{imm - 1, w}},
                addr});
    auto masked = newTemp(tmp, w);
    out.append(
            {ignil::Opcode::AND, masked, {shifted, ignil::Const{1, w}}, addr});
    auto bit = newTemp(tmp, 1);
    out.append(
            {ignil::Opcode::NEQ, bit, {masked, ignil::Const{0, w}}, addr});
    out.append({ignil::Opcode::COPY, regCF_, {bit}, addr});
  };

  std::map<X86Flag, FlagSetter> flags{
          {X86Flag::CF, cfLastBitOut},
          {X86Flag::ZF, zfEqZero(result, addr)},
          {X86Flag::SF, sfSignBit(result, addr)},
  };
  // OF only architecturally defined for a 1-bit shift: a logical shift
  // right always clears the new sign bit, so "did the sign appear to
  // change" reduces to whatever the original sign bit was.
  if (imm == 1)
    flags.emplace(X86Flag::OF, [this, lhs,
                                addr](ignil::Block &out, std::uint32_t &tmp) {
      auto of = newTemp(tmp, 1);
      out.append({ignil::Opcode::SLT,
                  of,
                  {ignil::Value{lhs}, ignil::Const{0, lhs.width}},
                  addr});
      out.append({ignil::Opcode::COPY, regOF_, {of}, addr});
    });
  emitArithFlags(addr, out, tmp, flags);
}

void dragon::x86::X86IGNILLifter::liftSarR1(const llvm::MCInst &mc,
                                            std::uint64_t addr,
                                            ignil::Block &out,
                                            std::uint32_t &tmp) const {
  // op[0]=dst(out), op[1]=src(tied=dst) - no imm operand at all, the shift
  // count of 1 is implicit in the opcode. Synthesize that operand and
  // delegate to liftSarImm so the flag formulas live in exactly one place.
  llvm::MCInst synthetic = mc;
  synthetic.addOperand(llvm::MCOperand::createImm(1));
  liftSarImm(synthetic, addr, out, tmp);
}

void dragon::x86::X86IGNILLifter::liftShlR1(const llvm::MCInst &mc,
                                            std::uint64_t addr,
                                            ignil::Block &out,
                                            std::uint32_t &tmp) const {
  llvm::MCInst synthetic = mc;
  synthetic.addOperand(llvm::MCOperand::createImm(1));
  liftShlImm(synthetic, addr, out, tmp);
}

void dragon::x86::X86IGNILLifter::liftShrR1(const llvm::MCInst &mc,
                                            std::uint64_t addr,
                                            ignil::Block &out,
                                            std::uint32_t &tmp) const {
  llvm::MCInst synthetic = mc;
  synthetic.addOperand(llvm::MCOperand::createImm(1));
  liftShrImm(synthetic, addr, out, tmp);
}

void dragon::x86::X86IGNILLifter::liftCvtsi2sd(const llvm::MCInst &mc,
                                               std::uint64_t addr,
                                               ignil::Block &out,
                                               std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftCvtsi2sd in addr {:#x} no dst", addr);
    return;
  }
  auto src = toReg(mc.getOperand(2).getReg());
  if (!src) {
    logger_.log("Error liftCvtsi2sd in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }
  out.append({ignil::Opcode::SITOFP, *dst, {*src}, addr});
}

void dragon::x86::X86IGNILLifter::liftCvttsd2si(const llvm::MCInst &mc,
                                                std::uint64_t addr,
                                                ignil::Block &out,
                                                std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftCvttsd2si in addr {:#x} no dst", addr);
    return;
  }
  auto src = toReg(mc.getOperand(1).getReg());
  if (!src) {
    logger_.log("Error liftCvttsd2si in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }
  out.append({ignil::Opcode::FPTOSI, *dst, {*src}, addr});
}

void dragon::x86::X86IGNILLifter::liftAddsd(const llvm::MCInst &mc,
                                            std::uint64_t addr,
                                            ignil::Block &out,
                                            std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftAddsd in addr {:#x} no dst", addr);
    return;
  }
  auto src = toReg(mc.getOperand(2).getReg());
  if (!src) {
    logger_.log("Error liftAddsd in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }
  out.append({ignil::Opcode::FADD, *dst, {*dst, *src}, addr});
}

void dragon::x86::X86IGNILLifter::liftSubsd(const llvm::MCInst &mc,
                                            std::uint64_t addr,
                                            ignil::Block &out,
                                            std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftSubsd in addr {:#x} no dst", addr);
    return;
  }
  auto src = toReg(mc.getOperand(2).getReg());
  if (!src) {
    logger_.log("Error liftSubsd in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }
  out.append({ignil::Opcode::FSUB, *dst, {*dst, *src}, addr});
}

void dragon::x86::X86IGNILLifter::liftMulsd(const llvm::MCInst &mc,
                                            std::uint64_t addr,
                                            ignil::Block &out,
                                            std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftMulsd in addr {:#x} no dst", addr);
    return;
  }
  auto src = toReg(mc.getOperand(2).getReg());
  if (!src) {
    logger_.log("Error liftMulsd in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }
  out.append({ignil::Opcode::FMUL, *dst, {*dst, *src}, addr});
}

void dragon::x86::X86IGNILLifter::liftMulsdMem(const llvm::MCInst &mc,
                                               std::uint64_t addr,
                                               ignil::Block &out,
                                               std::uint32_t &tmp) const {
  // op[0]=dst(out), op[1]=tied(=dst, ignored), op[2..6]=mem(rhs) - mirrors
  // liftAddRegMem's shape.
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftMulsdMem in addr {:#x} no dst", addr);
    return;
  }

  const ignil::Width ptrWidth =
          (architecture_ == Architecture::X86_64) ? 64 : 32;
  auto addrTemp = emitAddress(mc, 2, ptrWidth, addr, out, tmp);
  auto rhs = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::LOAD, rhs, {addrTemp}, addr});
  out.append({ignil::Opcode::FMUL, *dst, {*dst, rhs}, addr});
}

void dragon::x86::X86IGNILLifter::liftMovsdRR(const llvm::MCInst &mc,
                                              std::uint64_t addr,
                                              ignil::Block &out,
                                              std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftMovsdRR in addr {:#x} no dst", addr);
    return;
  }
  auto src = toReg(mc.getOperand(2).getReg());
  if (!src) {
    logger_.log("Error liftMovsdRR in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }
  out.append({ignil::Opcode::COPY, *dst, {*src}, addr});
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
