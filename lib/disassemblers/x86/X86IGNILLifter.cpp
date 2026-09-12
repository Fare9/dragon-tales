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
  regCL_ = findGprReg("CL");
  regAL_ = findGprReg("AL");
  regAX_ = findGprReg("AX");
  regEAX_ = findGprReg("EAX");
  regRAX_ = findGprReg("RAX");
  regDX_ = findGprReg("DX");
  regEDX_ = findGprReg("EDX");
  regEBX_ = findGprReg("EBX");
  regECX_ = findGprReg("ECX");
  regRDX_ = findGprReg("RDX");

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
          {"MOV16rm", &X86IGNILLifter::opMov16rm_},
          {"MOV8rm", &X86IGNILLifter::opMov8rm_},
          {"MOV64mr", &X86IGNILLifter::opMov64mr_},
          {"MOV32mr", &X86IGNILLifter::opMov32mr_},
          {"MOV16mr", &X86IGNILLifter::opMov16mr_},
          {"MOV8mr", &X86IGNILLifter::opMov8mr_},
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
          {"POPF64", &X86IGNILLifter::opPopf64_},
          {"PUSHF64", &X86IGNILLifter::opPushf64_},
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
          {"NOT8r", &X86IGNILLifter::opNot8r_},
          {"NOT16r", &X86IGNILLifter::opNot16r_},
          {"NOT32r", &X86IGNILLifter::opNot32r_},
          {"NOT64r", &X86IGNILLifter::opNot64r_},
          {"NEG8r", &X86IGNILLifter::opNeg8r_},
          {"NEG16r", &X86IGNILLifter::opNeg16r_},
          {"SHL8ri", &X86IGNILLifter::opShl8ri_},
          {"SHL8r1", &X86IGNILLifter::opShl8r1_},
          {"SHL16ri", &X86IGNILLifter::opShl16ri_},
          {"SHL16r1", &X86IGNILLifter::opShl16r1_},
          {"SHL8rCL", &X86IGNILLifter::opShl8rCL_},
          {"SHL16rCL", &X86IGNILLifter::opShl16rCL_},
          {"SHL32rCL", &X86IGNILLifter::opShl32rCL_},
          {"SHL64rCL", &X86IGNILLifter::opShl64rCL_},
          {"SHR8ri", &X86IGNILLifter::opShr8ri_},
          {"SHR8r1", &X86IGNILLifter::opShr8r1_},
          {"SHR16ri", &X86IGNILLifter::opShr16ri_},
          {"SHR16r1", &X86IGNILLifter::opShr16r1_},
          {"SHR8rCL", &X86IGNILLifter::opShr8rCL_},
          {"SHR16rCL", &X86IGNILLifter::opShr16rCL_},
          {"SHR32rCL", &X86IGNILLifter::opShr32rCL_},
          {"SHR64rCL", &X86IGNILLifter::opShr64rCL_},
          {"SAR8ri", &X86IGNILLifter::opSar8ri_},
          {"SAR8r1", &X86IGNILLifter::opSar8r1_},
          {"SAR16ri", &X86IGNILLifter::opSar16ri_},
          {"SAR16r1", &X86IGNILLifter::opSar16r1_},
          {"SAR8rCL", &X86IGNILLifter::opSar8rCL_},
          {"SAR16rCL", &X86IGNILLifter::opSar16rCL_},
          {"SAR32rCL", &X86IGNILLifter::opSar32rCL_},
          {"SAR64rCL", &X86IGNILLifter::opSar64rCL_},
          {"CBW", &X86IGNILLifter::opCbw_},
          {"CWDE", &X86IGNILLifter::opCwde_},
          {"CDQE", &X86IGNILLifter::opCdqe_},
          {"CWD", &X86IGNILLifter::opCwd_},
          {"CDQ", &X86IGNILLifter::opCdq_},
          {"CQO", &X86IGNILLifter::opCqo_},
          {"STC", &X86IGNILLifter::opStc_},
          {"CLC", &X86IGNILLifter::opClc_},
          {"CMC", &X86IGNILLifter::opCmc_},
          {"CPUID", &X86IGNILLifter::opCpuid_},
          {"RDTSC", &X86IGNILLifter::opRdtsc_},
          {"RDTSCP", &X86IGNILLifter::opRdtscp_},
          {"PUSH32i", &X86IGNILLifter::opPush32I_},
          {"PUSH32i8", &X86IGNILLifter::opPush32I8_},
          {"PUSH64i32", &X86IGNILLifter::opPush64I32_},
          {"PUSH64i8", &X86IGNILLifter::opPush64I8_},
          {"MOV8rr", &X86IGNILLifter::opMov8rr_},
          {"MOV8ri", &X86IGNILLifter::opMov8ri_},
          {"ADD8rr", &X86IGNILLifter::opAdd8rr_},
          {"ADD8ri", &X86IGNILLifter::opAdd8ri_},
          {"ADD8ri8", &X86IGNILLifter::opAdd8ri8_},
          {"SUB8rr", &X86IGNILLifter::opSub8rr_},
          {"SUB8ri", &X86IGNILLifter::opSub8ri_},
          {"SUB8ri8", &X86IGNILLifter::opSub8ri8_},
          {"AND8rr", &X86IGNILLifter::opAnd8rr_},
          {"AND8ri", &X86IGNILLifter::opAnd8ri_},
          {"AND8ri8", &X86IGNILLifter::opAnd8ri8_},
          {"OR8rr", &X86IGNILLifter::opOr8rr_},
          {"OR8ri", &X86IGNILLifter::opOr8ri_},
          {"OR8ri8", &X86IGNILLifter::opOr8ri8_},
          {"XOR8rr", &X86IGNILLifter::opXor8rr_},
          {"XOR8ri", &X86IGNILLifter::opXor8ri_},
          {"XOR8ri8", &X86IGNILLifter::opXor8ri8_},
          {"CMP8rr", &X86IGNILLifter::opCmp8rr_},
          {"CMP8ri", &X86IGNILLifter::opCmp8ri_},
          {"CMP8ri8", &X86IGNILLifter::opCmp8ri8_},
          {"TEST8rr", &X86IGNILLifter::opTest8rr_},
          {"TEST8ri", &X86IGNILLifter::opTest8ri_},
          {"INC8r", &X86IGNILLifter::opInc8r_},
          {"DEC8r", &X86IGNILLifter::opDec8r_},
          {"MOV16rr", &X86IGNILLifter::opMov16rr_},
          {"MOV16ri", &X86IGNILLifter::opMov16ri_},
          {"ADD16rr", &X86IGNILLifter::opAdd16rr_},
          {"ADD16ri", &X86IGNILLifter::opAdd16ri_},
          {"ADD16ri8", &X86IGNILLifter::opAdd16ri8_},
          {"SUB16rr", &X86IGNILLifter::opSub16rr_},
          {"SUB16ri", &X86IGNILLifter::opSub16ri_},
          {"SUB16ri8", &X86IGNILLifter::opSub16ri8_},
          {"AND16rr", &X86IGNILLifter::opAnd16rr_},
          {"AND16ri", &X86IGNILLifter::opAnd16ri_},
          {"AND16ri8", &X86IGNILLifter::opAnd16ri8_},
          {"OR16rr", &X86IGNILLifter::opOr16rr_},
          {"OR16ri", &X86IGNILLifter::opOr16ri_},
          {"OR16ri8", &X86IGNILLifter::opOr16ri8_},
          {"XOR16rr", &X86IGNILLifter::opXor16rr_},
          {"XOR16ri", &X86IGNILLifter::opXor16ri_},
          {"XOR16ri8", &X86IGNILLifter::opXor16ri8_},
          {"CMP16rr", &X86IGNILLifter::opCmp16rr_},
          {"CMP16ri", &X86IGNILLifter::opCmp16ri_},
          {"CMP16ri8", &X86IGNILLifter::opCmp16ri8_},
          {"TEST16rr", &X86IGNILLifter::opTest16rr_},
          {"TEST16ri", &X86IGNILLifter::opTest16ri_},
          {"INC16r", &X86IGNILLifter::opInc16r_},
          {"DEC16r", &X86IGNILLifter::opDec16r_},
          {"XCHG8rr", &X86IGNILLifter::opXchg8rr_},
          {"XCHG16rr", &X86IGNILLifter::opXchg16rr_},
          {"XCHG32rr", &X86IGNILLifter::opXchg32rr_},
          {"XCHG64rr", &X86IGNILLifter::opXchg64rr_},
          {"BSWAP32r", &X86IGNILLifter::opBswap32r_},
          {"BSWAP64r", &X86IGNILLifter::opBswap64r_},
          {"SHLD16rrCL", &X86IGNILLifter::opShld16rrCL_},
          {"SHLD16rri8", &X86IGNILLifter::opShld16rri8_},
          {"SHRD16rrCL", &X86IGNILLifter::opShrd16rrCL_},
          {"SHRD16rri8", &X86IGNILLifter::opShrd16rri8_},
          {"SHLD32rrCL", &X86IGNILLifter::opShld32rrCL_},
          {"SHLD32rri8", &X86IGNILLifter::opShld32rri8_},
          {"SHRD32rrCL", &X86IGNILLifter::opShrd32rrCL_},
          {"SHRD32rri8", &X86IGNILLifter::opShrd32rri8_},
          {"SHLD64rrCL", &X86IGNILLifter::opShld64rrCL_},
          {"SHLD64rri8", &X86IGNILLifter::opShld64rri8_},
          {"SHRD64rrCL", &X86IGNILLifter::opShrd64rrCL_},
          {"SHRD64rri8", &X86IGNILLifter::opShrd64rri8_},
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

  // *_REV load-direction encodings mapped to their base form (see header).
  // Only the widths whose base rr handler exists (32/64) are listed; an
  // unmapped base would just leave the *_REV as-is (still UNDEF, no harm).
  static const std::pair<std::string_view, std::string_view> kRevAliases[] = {
          {"MOV32rr_REV", "MOV32rr"}, {"MOV64rr_REV", "MOV64rr"},
          {"ADD32rr_REV", "ADD32rr"}, {"ADD64rr_REV", "ADD64rr"},
          {"SUB32rr_REV", "SUB32rr"}, {"SUB64rr_REV", "SUB64rr"},
          {"XOR32rr_REV", "XOR32rr"}, {"XOR64rr_REV", "XOR64rr"},
          {"AND32rr_REV", "AND32rr"}, {"AND64rr_REV", "AND64rr"},
          {"OR32rr_REV", "OR32rr"},   {"OR64rr_REV", "OR64rr"},
          {"CMP32rr_REV", "CMP32rr"}, {"CMP64rr_REV", "CMP64rr"},
          {"MOV8rr_REV", "MOV8rr"},   {"MOV16rr_REV", "MOV16rr"},
          {"ADD8rr_REV", "ADD8rr"},   {"ADD16rr_REV", "ADD16rr"},
          {"SUB8rr_REV", "SUB8rr"},   {"SUB16rr_REV", "SUB16rr"},
          {"XOR8rr_REV", "XOR8rr"},   {"XOR16rr_REV", "XOR16rr"},
          {"AND8rr_REV", "AND8rr"},   {"AND16rr_REV", "AND16rr"},
          {"OR8rr_REV", "OR8rr"},     {"OR16rr_REV", "OR16rr"},
          {"CMP8rr_REV", "CMP8rr"},   {"CMP16rr_REV", "CMP16rr"},
  };

  std::unordered_map<std::string_view, unsigned> nameToOpc;
  for (unsigned i = 0; i < instrInfo.getNumOpcodes(); ++i) {
    const std::string_view name = instrInfo.getName(i);
    nameToOpc.emplace(name, i);

    if (auto it = kOpcodeFields.find(name); it != kOpcodeFields.end())
      this->*(it->second) = i;
    else if (auto zx = movzxByName.find(name); zx != movzxByName.end())
      movzxForms_.emplace(i, zx->second);
    else if (auto sx = movsxByName.find(name); sx != movsxByName.end())
      movsxForms_.emplace(i, sx->second);
  }

  for (const auto &[rev, base]: kRevAliases) {
    auto r = nameToOpc.find(rev), b = nameToOpc.find(base);
    if (r != nameToOpc.end() && b != nameToOpc.end())
      revToBase_.emplace(r->second, b->second);
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

  // Normalize load-direction *_REV reg-reg encodings to their base opcode;
  // they share MCInst operand order, so the base handler lifts them correctly.
  if (auto it = revToBase_.find(opc); it != revToBase_.end())
    opc = it->second;

  if (opc == opMov64rr_ || opc == opMov32rr_ || opc == opMov64ri_ ||
      opc == opMov32ri_ || opc == opMov64ri32_)
    liftMov(*mc, addr, out, tempId);
  else if (opc == opMov64rm_ || opc == opMov32rm_ || opc == opMov16rm_ ||
           opc == opMov8rm_)
    liftMovLoad(*mc, addr, out, tempId);
  else if (opc == opMov64mr_ || opc == opMov32mr_ || opc == opMov16mr_ ||
           opc == opMov8mr_ || opc == opMov64mi_ || opc == opMov32mi_)
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
  else if (opc == opPopf64_)
    liftPopf(*mc, addr, out, tempId);
  else if (opc == opPushf64_)
    liftPushf(*mc, addr, out, tempId);
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
  // LifterGaps.md #7 round 2 - 8/16-bit forms of the generic lifters.
  else if (opc == opMov8rr_ || opc == opMov16rr_ || opc == opMov8ri_ ||
           opc == opMov16ri_)
    liftMov(*mc, addr, out, tempId);
  else if (opc == opAdd8rr_ || opc == opAdd8ri_ || opc == opAdd8ri8_ ||
           opc == opAdd16rr_ || opc == opAdd16ri_ || opc == opAdd16ri8_)
    liftAdd(*mc, addr, out, tempId);
  else if (opc == opSub8rr_ || opc == opSub8ri_ || opc == opSub8ri8_ ||
           opc == opSub16rr_ || opc == opSub16ri_ || opc == opSub16ri8_)
    liftSub(*mc, addr, out, tempId);
  else if (opc == opAnd8rr_ || opc == opAnd8ri_ || opc == opAnd8ri8_ ||
           opc == opAnd16rr_ || opc == opAnd16ri_ || opc == opAnd16ri8_)
    liftAnd(*mc, addr, out, tempId);
  else if (opc == opOr8rr_ || opc == opOr8ri_ || opc == opOr8ri8_ ||
           opc == opOr16rr_ || opc == opOr16ri_ || opc == opOr16ri8_)
    liftOr(*mc, addr, out, tempId);
  else if (opc == opXor8rr_ || opc == opXor8ri_ || opc == opXor8ri8_ ||
           opc == opXor16rr_ || opc == opXor16ri_ || opc == opXor16ri8_)
    liftXor(*mc, addr, out, tempId);
  else if (opc == opCmp8rr_ || opc == opCmp8ri_ || opc == opCmp8ri8_ ||
           opc == opCmp16rr_ || opc == opCmp16ri_ || opc == opCmp16ri8_)
    liftCmp(*mc, addr, out, tempId);
  else if (opc == opTest8rr_ || opc == opTest16rr_ || opc == opTest8ri_ ||
           opc == opTest16ri_)
    liftTest(*mc, addr, out, tempId);
  else if (opc == opInc8r_ || opc == opInc16r_)
    liftInc(*mc, addr, out, tempId);
  else if (opc == opDec8r_ || opc == opDec16r_)
    liftDec(*mc, addr, out, tempId);
  else if (opc == opXchg8rr_ || opc == opXchg16rr_ || opc == opXchg32rr_ ||
           opc == opXchg64rr_)
    liftXchg(*mc, addr, out, tempId);
  else if (opc == opBswap32r_ || opc == opBswap64r_)
    liftBswap(*mc, addr, out, tempId);
  else if (opc == opShld16rrCL_ || opc == opShld32rrCL_ || opc == opShld64rrCL_)
    liftShiftDouble(*mc, addr, out, tempId, /*left=*/true, /*byCL=*/true);
  else if (opc == opShrd16rrCL_ || opc == opShrd32rrCL_ || opc == opShrd64rrCL_)
    liftShiftDouble(*mc, addr, out, tempId, /*left=*/false, /*byCL=*/true);
  else if (opc == opShld16rri8_ || opc == opShld32rri8_ || opc == opShld64rri8_)
    liftShiftDouble(*mc, addr, out, tempId, /*left=*/true, /*byCL=*/false);
  else if (opc == opShrd16rri8_ || opc == opShrd32rri8_ || opc == opShrd64rri8_)
    liftShiftDouble(*mc, addr, out, tempId, /*left=*/false, /*byCL=*/false);
  else if (opc == opPush32I_ || opc == opPush32I8_ || opc == opPush64I32_ || opc == opPush64I8_)
    liftPushImm(*mc, addr, out, tempId);
  // LifterGaps.md #7 - VMProtect handler forms.
  else if (opc == opNot8r_ || opc == opNot16r_ || opc == opNot32r_ ||
           opc == opNot64r_)
    liftNot(*mc, addr, out, tempId);
  else if (opc == opNeg8r_ || opc == opNeg16r_)
    liftNeg(*mc, addr, out, tempId);
  else if (opc == opShl8ri_ || opc == opShl16ri_)
    liftShlImm(*mc, addr, out, tempId);
  else if (opc == opShr8ri_ || opc == opShr16ri_)
    liftShrImm(*mc, addr, out, tempId);
  else if (opc == opSar8ri_ || opc == opSar16ri_)
    liftSarImm(*mc, addr, out, tempId);
  else if (opc == opShl8r1_ || opc == opShl16r1_)
    liftShlR1(*mc, addr, out, tempId);
  else if (opc == opShr8r1_ || opc == opShr16r1_)
    liftShrR1(*mc, addr, out, tempId);
  else if (opc == opSar8r1_ || opc == opSar16r1_)
    liftSarR1(*mc, addr, out, tempId);
  else if (opc == opShl8rCL_ || opc == opShl16rCL_ || opc == opShl32rCL_ ||
           opc == opShl64rCL_)
    liftShiftCL(*mc, addr, out, tempId, ignil::Opcode::SHL);
  else if (opc == opShr8rCL_ || opc == opShr16rCL_ || opc == opShr32rCL_ ||
           opc == opShr64rCL_)
    liftShiftCL(*mc, addr, out, tempId, ignil::Opcode::SHR);
  else if (opc == opSar8rCL_ || opc == opSar16rCL_ || opc == opSar32rCL_ ||
           opc == opSar64rCL_)
    liftShiftCL(*mc, addr, out, tempId, ignil::Opcode::SAR);
  else if (opc == opCbw_)
    liftAccExtend(addr, out, tempId, AccExtend::CBW);
  else if (opc == opCwde_)
    liftAccExtend(addr, out, tempId, AccExtend::CWDE);
  else if (opc == opCdqe_)
    liftAccExtend(addr, out, tempId, AccExtend::CDQE);
  else if (opc == opCwd_)
    liftAccExtend(addr, out, tempId, AccExtend::CWD);
  else if (opc == opCdq_)
    liftAccExtend(addr, out, tempId, AccExtend::CDQ);
  else if (opc == opCqo_)
    liftAccExtend(addr, out, tempId, AccExtend::CQO);
  else if (opc == opStc_)
    liftCarryOp(addr, out, tempId, CarryOp::STC);
  else if (opc == opClc_)
    liftCarryOp(addr, out, tempId, CarryOp::CLC);
  else if (opc == opCmc_)
    liftCarryOp(addr, out, tempId, CarryOp::CMC);
  else if (opc == opCpuid_)
    liftOpaque(addr, out, tempId, ignil::OpaqueKind::CPUID);
  else if (opc == opRdtsc_)
    liftOpaque(addr, out, tempId, ignil::OpaqueKind::RDTSC);
  else if (opc == opRdtscp_)
    liftOpaque(addr, out, tempId, ignil::OpaqueKind::RDTSCP);
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
                         {X86Flag::PF, pfParity(result, addr)},
                         {X86Flag::AF, afNibbleCarry(lhs, src, result, addr)},
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
                         {X86Flag::PF, pfParity(result, addr)},
                         {X86Flag::AF, afNibbleCarry(lhs, addend, result, addr)},
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
                         {X86Flag::PF, pfParity(result, addr)},
                         {X86Flag::AF, afNibbleCarry(lhs, rhs, result, addr)},
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
                         {X86Flag::PF, pfParity(result, addr)},
                         {X86Flag::AF, afNibbleCarry(lhs, src, result, addr)},
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
                         {X86Flag::PF, pfParity(result, addr)},
                         {X86Flag::AF, afNibbleCarry(lhs, rhs, result, addr)},
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
                         {X86Flag::PF, pfParity(result, addr)},
                         {X86Flag::AF, afNibbleCarry(lhs, src, result, addr)},
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
                         {X86Flag::PF, pfParity(result, addr)},
                         {X86Flag::AF, afAlwaysZero(addr)},
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
                         {X86Flag::PF, pfParity(result, addr)},
                         {X86Flag::AF, afAlwaysZero(addr)},
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
                         {X86Flag::PF, pfParity(result, addr)},
                         {X86Flag::AF, afAlwaysZero(addr)},
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

// POPFQ/PUSHFQ move RFLAGS to/from the stack. Only the flags IGNIL models are
// handled (CF/PF/AF/ZF/SF/DF/OF, at their RFLAGS bit positions below); the
// system bits (TF/IF/IOPL/NT/...) are not modeled - POPFQ drops them and
// PUSHFQ emits them as 0. Bit 1 of RFLAGS always reads as 1 (reserved), which
// PUSHFQ sets explicitly.
void dragon::x86::X86IGNILLifter::liftPopf(const llvm::MCInst & /*mc*/,
                                           std::uint64_t addr,
                                           ignil::Block &out,
                                           std::uint32_t &tmp) const {
  // POPFQ: t = memory[RSP]; scatter t's bits into the modeled flag registers;
  // RSP += 8. Unmodeled system bits in t are simply not read.
  const ignil::Width ptrWidth =
          (architecture_ == Architecture::X86_64) ? 64 : 32;
  ignil::Reg sp = (architecture_ == Architecture::X86_64) ? regRSP_ : regESP_;

  auto loaded = newTemp(tmp, ptrWidth);
  out.append({ignil::Opcode::LOAD, loaded, {sp}, addr});

  const std::array<std::pair<ignil::Reg, unsigned>, 7> flags = {{
          {regCF_, 0}, {regPF_, 2}, {regAF_, 4}, {regZF_, 6}, {regSF_, 7},
          {regDF_, 10}, {regOF_, 11},
  }};
  for (const auto &[flagReg, bit]: flags) {
    auto shifted = newTemp(tmp, ptrWidth);
    out.append({ignil::Opcode::SHR,
                shifted,
                {loaded, ignil::Const{bit, ptrWidth}},
                addr});
    auto masked = newTemp(tmp, ptrWidth);
    out.append({ignil::Opcode::AND,
                masked,
                {shifted, ignil::Const{1, ptrWidth}},
                addr});
    auto narrowed = newTemp(tmp, flagReg.width);
    out.append({ignil::Opcode::TRUNC, narrowed, {masked}, addr});
    out.append({ignil::Opcode::COPY, flagReg, {narrowed}, addr});
  }

  // RSP += pointer size (8 in 64-bit).
  auto newSP = newTemp(tmp, ptrWidth);
  out.append({ignil::Opcode::ADD,
              newSP,
              {sp, ignil::Const{static_cast<std::uint64_t>(ptrWidth / 8), ptrWidth}},
              addr});
  out.append({ignil::Opcode::COPY, sp, {newSP}, addr});
}

void dragon::x86::X86IGNILLifter::liftPushf(const llvm::MCInst & /*mc*/,
                                            std::uint64_t addr,
                                            ignil::Block &out,
                                            std::uint32_t &tmp) const {
  // PUSHFQ: gather the modeled flags into an RFLAGS word (reserved bit 1 = 1,
  // unmodeled system bits = 0); RSP -= 8; memory[RSP] = word.
  const ignil::Width ptrWidth =
          (architecture_ == Architecture::X86_64) ? 64 : 32;
  ignil::Reg sp = (architecture_ == Architecture::X86_64) ? regRSP_ : regESP_;

  const std::array<std::pair<ignil::Reg, unsigned>, 7> flags = {{
          {regCF_, 0}, {regPF_, 2}, {regAF_, 4}, {regZF_, 6}, {regSF_, 7},
          {regDF_, 10}, {regOF_, 11},
  }};
  ignil::Value acc = ignil::Const{2, ptrWidth}; // reserved bit 1
  for (const auto &[flagReg, bit]: flags) {
    auto widened = newTemp(tmp, ptrWidth);
    out.append({ignil::Opcode::ZEXT, widened, {flagReg}, addr});
    auto shifted = newTemp(tmp, ptrWidth);
    out.append({ignil::Opcode::SHL,
                shifted,
                {widened, ignil::Const{bit, ptrWidth}},
                addr});
    auto combined = newTemp(tmp, ptrWidth);
    out.append({ignil::Opcode::OR, combined, {acc, shifted}, addr});
    acc = combined;
  }

  // RSP -= pointer size, then store the word.
  auto newSP = newTemp(tmp, ptrWidth);
  out.append({ignil::Opcode::SUB,
              newSP,
              {sp, ignil::Const{static_cast<std::uint64_t>(ptrWidth / 8), ptrWidth}},
              addr});
  out.append({ignil::Opcode::COPY, sp, {newSP}, addr});
  out.append({ignil::Opcode::STORE, std::nullopt, {sp, acc}, addr});
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
                         {X86Flag::PF, pfParity(result, addr)},
                         {X86Flag::AF, afNibbleCarry(*lhs, rhs, result, addr)},
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
                         {X86Flag::PF, pfParity(result, addr)},
                         {X86Flag::AF, afNibbleCarry(*lhs, rhs, result, addr)},
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
                         {X86Flag::PF, pfParity(result, addr)},
                         {X86Flag::AF, afNibbleCarry(lhs, rhs, result, addr)},
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
                         {X86Flag::PF, pfParity(result, addr)},
                         {X86Flag::AF, afAlwaysZero(addr)},
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
          {X86Flag::PF, pfParity(result, addr)},
          {X86Flag::AF, afAlwaysZero(addr)},
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
  pfParity(result, addr)(out, tmp);
  afNibbleCarry(ignil::Value{lhs}, ignil::Const{1, dst->width}, result, addr)(out, tmp);
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
  pfParity(result, addr)(out, tmp);
  afNibbleCarry(ignil::Value{lhs}, ignil::Const{1, dst->width}, result, addr)(out, tmp);
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
          {X86Flag::PF, pfParity(result, addr)},
          // AF for NEG: borrow into bit 4 of 0 - src.
          {X86Flag::AF,
           afNibbleCarry(ignil::Const{0, lhs.width}, ignil::Value{lhs}, result,
                         addr)},
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
          {X86Flag::PF, pfParity(result, addr)},
          {X86Flag::AF, afAlwaysZero(addr)},
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
          {X86Flag::PF, pfParity(result, addr)},
          {X86Flag::AF, afAlwaysZero(addr)},
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
    // No arithmetic/logic instruction touches DF (only STD/CLD/POPF do), so
    // an absent DF setter means "unchanged", not "undefined".
    if (flag == X86Flag::DF && it == flagMap.end())
      continue;
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

// -----------------------------------------------------------------------
// LifterGaps.md #7 - forms VMProtect handlers lean on: NOT, shifts by CL,
// 8/16-bit shifts (via the existing imm/r1 lifters), NEG8/16 (existing
// liftNeg), accumulator sign-extends, STC/CLC/CMC.
// -----------------------------------------------------------------------

void dragon::x86::X86IGNILLifter::liftNot(const llvm::MCInst &mc,
                                          std::uint64_t addr,
                                          ignil::Block &out,
                                          std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftNot in addr {:#x} no dst", addr);
    return;
  }
  auto src = toReg(mc.getOperand(1).getReg());
  if (!src) {
    logger_.log("Error liftNot in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }
  auto lhs = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::COPY, lhs, {*src}, addr});
  auto result = newTemp(tmp, dst->width);
  out.append({ignil::Opcode::NOT, result, {ignil::Value{lhs}}, addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});
}

void dragon::x86::X86IGNILLifter::liftShiftCL(const llvm::MCInst &mc,
                                              std::uint64_t addr,
                                              ignil::Block &out,
                                              std::uint32_t &tmp,
                                              ignil::Opcode shift) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftShiftCL in addr {:#x} no dst", addr);
    return;
  }
  auto src = toReg(mc.getOperand(1).getReg());
  if (!src) {
    logger_.log("Error liftShiftCL in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }
  const ignil::Width w = dst->width;
  const std::uint64_t W = w;
  const std::uint64_t countMask = (w == 64) ? 63 : 31;

  auto lhs = newTemp(tmp, w);
  out.append({ignil::Opcode::COPY, lhs, {*src}, addr});
  auto cnt8 = newTemp(tmp, 8);
  out.append({ignil::Opcode::AND,
              cnt8,
              {regCL_, ignil::Const{countMask, 8}},
              addr});
  auto cnt = newTemp(tmp, w);
  if (w == 8)
    out.append({ignil::Opcode::COPY, cnt, {ignil::Value{cnt8}}, addr});
  else
    out.append({ignil::Opcode::ZEXT, cnt, {ignil::Value{cnt8}}, addr});
  auto result = newTemp(tmp, w);
  out.append({shift, result, {ignil::Value{lhs}, ignil::Value{cnt}}, addr});
  out.append({ignil::Opcode::COPY, *dst, {result}, addr});

  emitGatedShiftFlags(addr, out, tmp, lhs, cnt, result,
                      shift == ignil::Opcode::SHL);
}

void dragon::x86::X86IGNILLifter::liftAccExtend(std::uint64_t addr,
                                                ignil::Block &out,
                                                std::uint32_t &tmp,
                                                AccExtend kind) const {
  auto signFill = [&](ignil::Reg from, ignil::Reg to, std::uint64_t bits) {
    auto t = newTemp(tmp, from.width);
    out.append({ignil::Opcode::COPY, t, {from}, addr});
    auto r = newTemp(tmp, from.width);
    out.append({ignil::Opcode::SAR,
                r,
                {ignil::Value{t}, ignil::Const{bits, from.width}},
                addr});
    out.append({ignil::Opcode::COPY, to, {ignil::Value{r}}, addr});
  };
  switch (kind) {
    case AccExtend::CBW:
      out.append({ignil::Opcode::SEXT, regAX_, {regAL_}, addr});
      return;
    case AccExtend::CWDE:
      out.append({ignil::Opcode::SEXT, regEAX_, {regAX_}, addr});
      return;
    case AccExtend::CDQE:
      out.append({ignil::Opcode::SEXT, regRAX_, {regEAX_}, addr});
      return;
    case AccExtend::CWD:
      signFill(regAX_, regDX_, 15);
      return;
    case AccExtend::CDQ:
      signFill(regEAX_, regEDX_, 31);
      return;
    case AccExtend::CQO:
      signFill(regRAX_, regRDX_, 63);
      return;
  }
}

void dragon::x86::X86IGNILLifter::liftOpaque(std::uint64_t addr,
                                             ignil::Block &out,
                                             std::uint32_t &tmp,
                                             ignil::OpaqueKind kind) const {
  // Snapshot the inputs first: CPUID reads EAX/ECX and also defines them, so
  // every result must be a function of the values on entry rather than of
  // whatever an earlier result op has just stored.
  std::vector<ignil::Value> srcs;
  auto snapshot = [&](const ignil::Reg &r) {
    auto t = newTemp(tmp, r.width);
    out.append({ignil::Opcode::COPY, t, {r}, addr});
    srcs.push_back(ignil::Value{t});
  };

  std::vector<ignil::Reg> results;
  switch (kind) {
    case ignil::OpaqueKind::CPUID:
      snapshot(regEAX_); // leaf
      snapshot(regECX_); // subleaf
      results = {regEAX_, regEBX_, regECX_, regEDX_};
      break;
    case ignil::OpaqueKind::RDTSC:
      results = {regEAX_, regEDX_}; // EDX:EAX
      break;
    case ignil::OpaqueKind::RDTSCP:
      results = {regEAX_, regEDX_, regECX_}; // EDX:EAX, ECX = processor id
      break;
  }

  for (unsigned i = 0; i < results.size(); ++i)
    out.append({ignil::Opcode::OPAQUE, results[i], srcs, addr,
                ignil::makeOpaqueAux(kind, i)});
}

void dragon::x86::X86IGNILLifter::liftCarryOp(std::uint64_t addr,
                                              ignil::Block &out,
                                              std::uint32_t &tmp,
                                              CarryOp kind) const {
  switch (kind) {
    case CarryOp::STC:
      out.append({ignil::Opcode::COPY, regCF_, {ignil::Const{1, 1}}, addr});
      return;
    case CarryOp::CLC:
      out.append({ignil::Opcode::COPY, regCF_, {ignil::Const{0, 1}}, addr});
      return;
    case CarryOp::CMC: {
      auto t = newTemp(tmp, 8);
      out.append({ignil::Opcode::XOR, t, {regCF_, ignil::Const{1, 8}}, addr});
      out.append({ignil::Opcode::COPY, regCF_, {ignil::Value{t}}, addr});
      return;
    }
  }
}

void dragon::x86::X86IGNILLifter::emitGatedShiftFlags(std::uint64_t addr,
                                                      ignil::Block &out,
                                                      std::uint32_t &tmp,
                                                      ignil::Temp lhs,
                                                      ignil::Temp cnt,
                                                      ignil::Temp result,
                                                      bool leftShift) const {
  const ignil::Width w = lhs.width;
  const std::uint64_t W = w;
  // A zero count leaves every flag untouched. IGNIL has no select, so each
  // new flag value is merged with the old one under 8-bit masks:
  //   flag = (old & isZero8) | (new & notZero8)
  auto isZero = newTemp(tmp, 1);
  out.append({ignil::Opcode::EQ,
              isZero,
              {ignil::Value{cnt}, ignil::Const{0, w}},
              addr});
  auto z8 = newTemp(tmp, 8);
  out.append({ignil::Opcode::ZEXT, z8, {ignil::Value{isZero}}, addr});
  auto nz8 = newTemp(tmp, 8);
  out.append({ignil::Opcode::XOR,
              nz8,
              {ignil::Value{z8}, ignil::Const{1, 8}},
              addr});
  auto gate = [&](ignil::Reg flag, ignil::Temp newBit) {
    auto new8 = newTemp(tmp, 8);
    out.append({ignil::Opcode::ZEXT, new8, {ignil::Value{newBit}}, addr});
    auto keep = newTemp(tmp, 8);
    out.append({ignil::Opcode::AND, keep, {flag, ignil::Value{z8}}, addr});
    auto take = newTemp(tmp, 8);
    out.append({ignil::Opcode::AND,
                take,
                {ignil::Value{new8}, ignil::Value{nz8}},
                addr});
    auto merged = newTemp(tmp, 8);
    out.append({ignil::Opcode::OR,
                merged,
                {ignil::Value{keep}, ignil::Value{take}},
                addr});
    out.append({ignil::Opcode::COPY, flag, {ignil::Value{merged}}, addr});
  };

  // CF = the last bit shifted out: bit (count-1) of the original value for
  // a right shift, bit (width-count) for a left shift. The index is masked
  // to width-1 so a zero count (whose CF the gate discards anyway) never
  // produces an out-of-range shift.
  auto rawIdx = newTemp(tmp, w);
  if (leftShift)
    out.append({ignil::Opcode::SUB,
                rawIdx,
                {ignil::Const{W, w}, ignil::Value{cnt}},
                addr});
  else
    out.append({ignil::Opcode::SUB,
                rawIdx,
                {ignil::Value{cnt}, ignil::Const{1, w}},
                addr});
  auto idx = newTemp(tmp, w);
  out.append({ignil::Opcode::AND,
              idx,
              {ignil::Value{rawIdx}, ignil::Const{W - 1, w}},
              addr});
  auto shifted = newTemp(tmp, w);
  out.append({ignil::Opcode::SHR,
              shifted,
              {ignil::Value{lhs}, ignil::Value{idx}},
              addr});
  auto bit = newTemp(tmp, w);
  out.append({ignil::Opcode::AND,
              bit,
              {ignil::Value{shifted}, ignil::Const{1, w}},
              addr});
  auto cf = newTemp(tmp, 1);
  out.append({ignil::Opcode::NEQ,
              cf,
              {ignil::Value{bit}, ignil::Const{0, w}},
              addr});
  gate(regCF_, cf);
  auto zf = newTemp(tmp, 1);
  out.append({ignil::Opcode::EQ,
              zf,
              {ignil::Value{result}, ignil::Const{0, w}},
              addr});
  gate(regZF_, zf);
  auto sf = newTemp(tmp, 1);
  out.append({ignil::Opcode::SLT,
              sf,
              {ignil::Value{result}, ignil::Const{0, w}},
              addr});
  gate(regSF_, sf);
  // OF is architecturally defined only for a count of 1, PF/AF are not
  // modeled: honestly UNDEF (a zero count would keep them, which the gate
  // cannot express for an undefined value).
  // PF from the result and AF cleared, both gated like CF/ZF/SF; OF is
  // architecturally defined only for a count of 1 and stays UNDEF.
  gate(regPF_, parityBit(ignil::Value{result}, addr, out, tmp));
  auto afZero = newTemp(tmp, 1);
  out.append({ignil::Opcode::COPY, afZero, {ignil::Const{0, 1}}, addr});
  gate(regAF_, afZero);
  emitUndef(regOF_, addr, out);
}

void dragon::x86::X86IGNILLifter::liftXchg(const llvm::MCInst &mc,
                                           std::uint64_t addr,
                                           ignil::Block &out,
                                           std::uint32_t &tmp) const {
  auto a = toReg(mc.getOperand(0).getReg());
  auto b = toReg(mc.getOperand(1).getReg());
  if (!a || !b) {
    logger_.log("Error liftXchg in addr {:#x} bad operands", addr);
    if (a) emitUndef(*a, addr, out);
    if (b) emitUndef(*b, addr, out);
    return;
  }
  auto ta = newTemp(tmp, a->width);
  out.append({ignil::Opcode::COPY, ta, {*a}, addr});
  auto tb = newTemp(tmp, b->width);
  out.append({ignil::Opcode::COPY, tb, {*b}, addr});
  out.append({ignil::Opcode::COPY, *a, {ignil::Value{tb}}, addr});
  out.append({ignil::Opcode::COPY, *b, {ignil::Value{ta}}, addr});
}

void dragon::x86::X86IGNILLifter::liftBswap(const llvm::MCInst &mc,
                                            std::uint64_t addr,
                                            ignil::Block &out,
                                            std::uint32_t &tmp) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftBswap in addr {:#x} no dst", addr);
    return;
  }
  const ignil::Width w = dst->width;
  const std::uint64_t n = w / 8;
  auto v = newTemp(tmp, w);
  out.append({ignil::Opcode::COPY, v, {*dst}, addr});
  std::optional<ignil::Temp> acc;
  for (std::uint64_t i = 0; i < n; ++i) {
    // byte i of the source lands at byte n-1-i of the result
    auto sh = newTemp(tmp, w);
    out.append({ignil::Opcode::SHR,
                sh,
                {ignil::Value{v}, ignil::Const{8 * i, w}},
                addr});
    auto byte = newTemp(tmp, w);
    out.append({ignil::Opcode::AND,
                byte,
                {ignil::Value{sh}, ignil::Const{0xff, w}},
                addr});
    auto placed = newTemp(tmp, w);
    out.append({ignil::Opcode::SHL,
                placed,
                {ignil::Value{byte}, ignil::Const{8 * (n - 1 - i), w}},
                addr});
    if (!acc) {
      acc = placed;
    } else {
      auto merged = newTemp(tmp, w);
      out.append({ignil::Opcode::OR,
                  merged,
                  {ignil::Value{*acc}, ignil::Value{placed}},
                  addr});
      acc = merged;
    }
  }
  out.append({ignil::Opcode::COPY, *dst, {ignil::Value{*acc}}, addr});
}

void dragon::x86::X86IGNILLifter::liftShiftDouble(const llvm::MCInst &mc,
                                                  std::uint64_t addr,
                                                  ignil::Block &out,
                                                  std::uint32_t &tmp,
                                                  bool left,
                                                  bool byCL) const {
  auto dst = toReg(mc.getOperand(0).getReg());
  if (!dst) {
    logger_.log("Error liftShiftDouble in addr {:#x} no dst", addr);
    return;
  }
  auto src1 = toReg(mc.getOperand(1).getReg());
  auto src2 = toReg(mc.getOperand(2).getReg());
  if (!src1 || !src2) {
    logger_.log("Error liftShiftDouble in addr {:#x} no src", addr);
    emitUndef(*dst, addr, out);
    return;
  }
  const ignil::Width w = dst->width;
  const std::uint64_t W = w;
  const std::uint64_t countMask = (w == 64) ? 63 : 31;

  auto lhs = newTemp(tmp, w);
  out.append({ignil::Opcode::COPY, lhs, {*src1}, addr});
  auto other = newTemp(tmp, w);
  out.append({ignil::Opcode::COPY, other, {*src2}, addr});
  auto cnt = newTemp(tmp, w);
  if (byCL) {
    auto cnt8 = newTemp(tmp, 8);
    out.append({ignil::Opcode::AND,
                cnt8,
                {regCL_, ignil::Const{countMask, 8}},
                addr});
    if (w == 8)
      out.append({ignil::Opcode::COPY, cnt, {ignil::Value{cnt8}}, addr});
    else
      out.append({ignil::Opcode::ZEXT, cnt, {ignil::Value{cnt8}}, addr});
  } else {
    const auto imm = static_cast<std::uint64_t>(mc.getOperand(3).getImm()) & countMask;
    out.append({ignil::Opcode::COPY, cnt, {ignil::Const{imm, w}}, addr});
  }
  // shifted = left  ? (lhs << cnt) | (other >> (W - cnt))
  //                 : (lhs >> cnt) | (other << (W - cnt))
  // The complementary count is masked to W-1 so a zero count shifts `other`
  // by 0 instead of by W (which LLVM treats as poison); that partial result
  // is discarded by the mask below anyway.
  auto comp = newTemp(tmp, w);
  out.append({ignil::Opcode::SUB,
              comp,
              {ignil::Const{W, w}, ignil::Value{cnt}},
              addr});
  auto compM = newTemp(tmp, w);
  out.append({ignil::Opcode::AND,
              compM,
              {ignil::Value{comp}, ignil::Const{W - 1, w}},
              addr});
  auto part1 = newTemp(tmp, w);
  out.append({left ? ignil::Opcode::SHL : ignil::Opcode::SHR,
              part1,
              {ignil::Value{lhs}, ignil::Value{cnt}},
              addr});
  auto part2 = newTemp(tmp, w);
  out.append({left ? ignil::Opcode::SHR : ignil::Opcode::SHL,
              part2,
              {ignil::Value{other}, ignil::Value{compM}},
              addr});
  auto shifted = newTemp(tmp, w);
  out.append({ignil::Opcode::OR,
              shifted,
              {ignil::Value{part1}, ignil::Value{part2}},
              addr});
  // result = cnt != 0 ? shifted : lhs, as (shifted & m) | (lhs & ~m) with
  // m = all-ones when cnt != 0.
  auto nz = newTemp(tmp, 1);
  out.append({ignil::Opcode::NEQ,
              nz,
              {ignil::Value{cnt}, ignil::Const{0, w}},
              addr});
  auto nzW = newTemp(tmp, w);
  out.append({ignil::Opcode::ZEXT, nzW, {ignil::Value{nz}}, addr});
  auto mask = newTemp(tmp, w);
  out.append({ignil::Opcode::NEG, mask, {ignil::Value{nzW}}, addr});
  auto keepMask = newTemp(tmp, w);
  out.append({ignil::Opcode::NOT, keepMask, {ignil::Value{mask}}, addr});
  auto take = newTemp(tmp, w);
  out.append({ignil::Opcode::AND,
              take,
              {ignil::Value{shifted}, ignil::Value{mask}},
              addr});
  auto keep = newTemp(tmp, w);
  out.append({ignil::Opcode::AND,
              keep,
              {ignil::Value{lhs}, ignil::Value{keepMask}},
              addr});
  auto result = newTemp(tmp, w);
  out.append({ignil::Opcode::OR,
              result,
              {ignil::Value{take}, ignil::Value{keep}},
              addr});
  out.append({ignil::Opcode::COPY, *dst, {ignil::Value{result}}, addr});
  emitGatedShiftFlags(addr, out, tmp, lhs, cnt, result, left);
}

void dragon::x86::X86IGNILLifter::liftPushImm(const llvm::MCInst &mc,
                                              std::uint64_t addr,
                                              ignil::Block &out,
                                              std::uint32_t &tmp) const {
  const bool is64 = (architecture_ == Architecture::X86_64);
  const ignil::Width w = is64 ? 64 : 32;
  const std::uint64_t bytes = is64 ? 8 : 4;
  ignil::Reg sp = is64 ? regRSP_ : regESP_;
  const auto imm = static_cast<std::uint64_t>(
          static_cast<std::int64_t>(mc.getOperand(0).getImm()));   // sign-extended
  auto newSP = newTemp(tmp, w);
  out.append({ignil::Opcode::SUB, newSP, {sp, ignil::Const{bytes, w}}, addr});
  out.append({ignil::Opcode::COPY, sp, {ignil::Value{newSP}}, addr});
  out.append({ignil::Opcode::STORE, std::nullopt, {sp, ignil::Const{imm & (is64 ? ~0ull : 0xffffffffull), w}}, addr});
}

dragon::ignil::Temp
dragon::x86::X86IGNILLifter::parityBit(ignil::Value result, std::uint64_t addr,
                                       ignil::Block &out,
                                       std::uint32_t &tmp) const {
  // PF is set when the low byte of the result has an even number of 1 bits:
  // fold the byte with three xor/shift steps, PF = (fold & 1) == 0.
  const ignil::Width w = ignil::widthOf(result);
  ignil::Value low = result;
  if (w > 8) {
    auto t = newTemp(tmp, 8);
    out.append({ignil::Opcode::TRUNC, t, {result}, addr});
    low = ignil::Value{t};
  }
  auto x1 = newTemp(tmp, 8);
  out.append({ignil::Opcode::SHR, x1, {low, ignil::Const{4, 8}}, addr});
  auto x2 = newTemp(tmp, 8);
  out.append({ignil::Opcode::XOR, x2, {low, ignil::Value{x1}}, addr});
  auto x3 = newTemp(tmp, 8);
  out.append({ignil::Opcode::SHR, x3, {ignil::Value{x2}, ignil::Const{2, 8}}, addr});
  auto x4 = newTemp(tmp, 8);
  out.append({ignil::Opcode::XOR, x4, {ignil::Value{x2}, ignil::Value{x3}}, addr});
  auto x5 = newTemp(tmp, 8);
  out.append({ignil::Opcode::SHR, x5, {ignil::Value{x4}, ignil::Const{1, 8}}, addr});
  auto x6 = newTemp(tmp, 8);
  out.append({ignil::Opcode::XOR, x6, {ignil::Value{x4}, ignil::Value{x5}}, addr});
  auto bit = newTemp(tmp, 8);
  out.append({ignil::Opcode::AND, bit, {ignil::Value{x6}, ignil::Const{1, 8}}, addr});
  auto pf = newTemp(tmp, 1);
  out.append({ignil::Opcode::EQ, pf, {ignil::Value{bit}, ignil::Const{0, 8}}, addr});
  return pf;
}

dragon::x86::FlagSetter
dragon::x86::X86IGNILLifter::pfParity(ignil::Value result,
                                      std::uint64_t addr) const {
  return [this, result, addr](ignil::Block &out, std::uint32_t &tmp) {
    auto pf = parityBit(result, addr, out, tmp);
    out.append({ignil::Opcode::COPY, regPF_, {ignil::Value{pf}}, addr});
  };
}

dragon::x86::FlagSetter
dragon::x86::X86IGNILLifter::afNibbleCarry(ignil::Value lhs, ignil::Value rhs,
                                           ignil::Value result,
                                           std::uint64_t addr) const {
  return [this, lhs, rhs, result, addr](ignil::Block &out, std::uint32_t &tmp) {
    // AF = bit 4 of (lhs ^ rhs ^ result): the carry/borrow across bit 3,
    // the same formula for add, sub, inc, dec and neg.
    const ignil::Width w = ignil::widthOf(result);
    auto t1 = newTemp(tmp, w);
    out.append({ignil::Opcode::XOR, t1, {lhs, rhs}, addr});
    auto t2 = newTemp(tmp, w);
    out.append({ignil::Opcode::XOR, t2, {ignil::Value{t1}, result}, addr});
    auto t3 = newTemp(tmp, w);
    out.append({ignil::Opcode::AND, t3, {ignil::Value{t2}, ignil::Const{0x10, w}}, addr});
    auto af = newTemp(tmp, 1);
    out.append({ignil::Opcode::NEQ, af, {ignil::Value{t3}, ignil::Const{0, w}}, addr});
    out.append({ignil::Opcode::COPY, regAF_, {ignil::Value{af}}, addr});
  };
}

dragon::x86::FlagSetter
dragon::x86::X86IGNILLifter::afAlwaysZero(std::uint64_t addr) const {
  return [this, addr](ignil::Block &out, std::uint32_t &) {
    out.append({ignil::Opcode::COPY, regAF_, {ignil::Const{0, 1}}, addr});
  };
}
