#include <dragon/IGNIL.hpp>

#include "utils/Utils.hpp"

#include <format>

dragon::ignil::Width dragon::ignil::widthOf(const Value &v) {
  return std::visit([](const auto &val) { return val.width; }, v);
}

dragon::ignil::Block::Block(std::uint64_t startAddress) :
    startAddress_(startAddress) {}

void dragon::ignil::Block::append(Op op) { ops_.push_back(std::move(op)); }

const std::vector<dragon::ignil::Op> &dragon::ignil::Block::ops() const {
  return ops_;
}
std::uint64_t dragon::ignil::Block::startAddress() const {
  return startAddress_;
}
bool dragon::ignil::Block::empty() const { return ops_.empty(); }
size_t dragon::ignil::Block::size() const { return ops_.size(); }
std::vector<dragon::ignil::Op>::const_iterator
dragon::ignil::Block::begin() const { return ops_.begin(); }
std::vector<dragon::ignil::Op>::const_iterator
dragon::ignil::Block::end() const { return ops_.end(); }

std::string_view dragon::ignil::opcodeName(Opcode op) {
  switch (op) {
    case Opcode::COPY:
      return "COPY";
    case Opcode::LOAD:
      return "LOAD";
    case Opcode::STORE:
      return "STORE";
    case Opcode::ADD:
      return "ADD";
    case Opcode::SUB:
      return "SUB";
    case Opcode::NEG:
      return "NEG";
    case Opcode::MUL:
      return "MUL";
    case Opcode::SDIV:
      return "SDIV";
    case Opcode::UDIV:
      return "UDIV";
    case Opcode::SMOD:
      return "SMOD";
    case Opcode::UMOD:
      return "UMOD";
    case Opcode::AND:
      return "AND";
    case Opcode::OR:
      return "OR";
    case Opcode::XOR:
      return "XOR";
    case Opcode::NOT:
      return "NOT";
    case Opcode::SHL:
      return "SHL";
    case Opcode::SHR:
      return "SHR";
    case Opcode::SAR:
      return "SAR";
    case Opcode::ZEXT:
      return "ZEXT";
    case Opcode::SEXT:
      return "SEXT";
    case Opcode::TRUNC:
      return "TRUNC";
    case Opcode::EQ:
      return "EQ";
    case Opcode::NEQ:
      return "NEQ";
    case Opcode::ULT:
      return "ULT";
    case Opcode::ULE:
      return "ULE";
    case Opcode::UGT:
      return "UGT";
    case Opcode::UGE:
      return "UGE";
    case Opcode::SLT:
      return "SLT";
    case Opcode::SLE:
      return "SLE";
    case Opcode::SGT:
      return "SGT";
    case Opcode::SGE:
      return "SGE";
    case Opcode::JMP:
      return "JMP";
    case Opcode::CJMP:
      return "CJMP";
    case Opcode::CALL:
      return "CALL";
    case Opcode::RET:
      return "RET";
    case Opcode::UNDEF:
      return "UNDEF";
  }
  return "?";
}

std::string dragon::ignil::toString(const Value &v) {
  return std::visit(utils::overloaded{[](const Const &c) {
                                        return std::format("0x{:x}:i{}",
                                                           c.value, c.width);
                                      },
                                      [](const Reg &r) {
                                        return std::format("reg[{}]:i{}",
                                                           r.offset, r.width);
                                      },
                                      [](const Temp &t) {
                                        return std::format("t{}:i{}", t.id,
                                                           t.width);
                                      }},
                    v);
}

std::string dragon::ignil::toString(const Op &op) {
  std::string s;
  if (op.dst)
    s += toString(*op.dst) + " = ";
  s += std::string(opcodeName(op.opcode));
  s += "(";
  for (std::size_t i = 0; i < op.srcs.size(); ++i) {
    if (i)
      s += ", ";
    s += toString(op.srcs[i]);
  }
  s += ")";
  if (op.address)
    s += std::format("  ; @0x{:x}", op.address);
  return s;
}

std::string dragon::ignil::toString(const Block &block) {
  std::string s = std::format("IGNILBlock @ 0x{:x}\n", block.startAddress());
  for (const auto &op: block)
    s += "  " + toString(op) + "\n";
  return s;
}
