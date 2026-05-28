#include <dragon/x86Info.hpp>

namespace {
bool x8632bitIsCommonPrologue(std::span<const dragon::Instruction *> insns) {
  if (insns.size() < 2)
    return false;
  if (insns[0]->mnemonic() != "push" && insns[0]->operands() != "ebp")
    return false;
  if (insns[1]->mnemonic() != "mov" && insns[1]->operands() != "ebp, esp")
    return false;
  return true;
}

bool x8664bitIsCommonPrologue(std::span<const dragon::Instruction *> insns) {
  if (insns.size() < 2)
    return false;
  if (insns[0]->mnemonic() != "push" || insns[0]->operands() != "rbp")
    return false;
  if (insns[1]->mnemonic() != "mov" || insns[1]->operands() != "rbp, rsp")
    return false;
  return true;
}

bool x8632bitIsCommonEpilogue(std::span<const dragon::Instruction *> insns) {
  if (insns.size() < 2)
    return false;
  const bool popRet = insns[0]->mnemonic() == "pop" &&
                      insns[0]->operands() == "ebp" &&
                      insns[1]->mnemonic() == "ret";
  const bool leaveRet =
          insns[0]->mnemonic() == "leave" && insns[1]->mnemonic() == "ret";
  return popRet || leaveRet;
}

bool x8664bitIsCommonEpilogue(std::span<const dragon::Instruction *> insns) {
  if (insns.size() < 2)
    return false;
  const bool popRet = insns[0]->mnemonic() == "pop" &&
                      insns[0]->operands() == "rbp" &&
                      insns[1]->mnemonic() == "ret";
  const bool leaveRet =
          insns[0]->mnemonic() == "leave" && insns[1]->mnemonic() == "ret";
  return popRet || leaveRet;
}

} // namespace

dragon::X86Info::X86Info(Architecture architecture) :
    architecture_(architecture) {}

bool dragon::X86Info::isCommonFunctionPrologue(
        std::span<const Instruction *> insns) const {
  if (architecture_ == Architecture::X86_32)
    return ::x8632bitIsCommonPrologue(insns);
  if (architecture_ == Architecture::X86_64)
    return ::x8664bitIsCommonPrologue(insns);
  return false;
}
bool dragon::X86Info::isCommonFunctionEpilogue(
        std::span<const Instruction *> insns) const {
  if (architecture_ == Architecture::X86_32)
    return ::x8632bitIsCommonEpilogue(insns);
  if (architecture_ == Architecture::X86_64)
    return ::x8664bitIsCommonEpilogue(insns);
  return false;
}

bool dragon::X86Info::isTrap(const Instruction &insn) {
  const auto m = insn.mnemonic();
  return m == "int3" || m == "ud2" || m == "ud1";
}

bool dragon::X86Info::isNop(const Instruction &insn) {
  const auto m = insn.mnemonic();
  return m == "nop" || m == "fnop";
}

bool dragon::X86Info::isInterruptReturn(const Instruction &insn) {
  const auto m = insn.mnemonic();
  return m == "iret" || m == "iretd" || m == "iretq";
}

bool dragon::X86Info::isHalt(const Instruction &insn) {
  return insn.mnemonic() == "hlt";
}
