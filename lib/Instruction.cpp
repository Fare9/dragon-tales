#include <dragon/Instruction.hpp>

dragon::Instruction::Instruction(std::uint64_t address, std::uint8_t size,
                                 std::string mnemonic, std::string operands,
                                 std::vector<std::uint8_t> bytes, bool isReturn,
                                 bool isCall, bool isJump, bool isConditional,
                                 bool isTrap, bool hasIndirectTarget) :
    address_(address), size_(size), mnemonic_(std::move(mnemonic)),
    operands_(std::move(operands)), bytes_(std::move(bytes)),
    isReturn_(isReturn), isCall_(isCall), isJump_(isJump),
    isConditional_(isConditional), isTrap_(isTrap),
    hasIndirectTarget_(hasIndirectTarget) {
  disassembly_ = mnemonic_;
  if (!operands_.empty()) {
    disassembly_ += ' ';
    disassembly_ += operands_;
  }
}
