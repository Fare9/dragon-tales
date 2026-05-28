#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace dragon {

/// A representation of an Instruction agnostic to any kind of architecture
/// the instructions will be populated during the disassembly, and they will
/// contain the data needed to know about the instruction. The instruction
/// itself will be represented as strings. The constructor receives the minimum
/// data and the other information will be provided using the setters (as we may
/// miss information at the beginning)
class Instruction {
  public:
  [[nodiscard]] std::uint64_t address() const { return address_; }
  [[nodiscard]] std::uint8_t size() const { return size_; }

  [[nodiscard]] std::string_view mnemonic() const { return mnemonic_; }
  [[nodiscard]] std::string_view operands() const { return operands_; }
  [[nodiscard]] std::string_view disassembly() const { return disassembly_; }
  [[nodiscard]] std::string_view pattern() const { return pattern_; }

  [[nodiscard]] const std::vector<std::uint8_t> &bytes() const {
    return bytes_;
  }
  [[nodiscard]] const std::vector<std::uint8_t> &chromosomeMask() const {
    return chromosomeMask_;
  }

  [[nodiscard]] const std::set<std::uint64_t> &branches() const {
    return branches_;
  }
  [[nodiscard]] const std::set<std::uint64_t> &callees() const {
    return callees_;
  }
  [[nodiscard]] std::size_t edges() const { return edges_; }

  // Structural flags
  [[nodiscard]] bool isBlockStart() const { return isBlockStart_; }
  [[nodiscard]] bool isFunctionStart() const { return isFunctionStart_; }
  [[nodiscard]] bool isPrologue() const { return isPrologue_; }
  [[nodiscard]] bool isReturn() const { return isReturn_; }
  [[nodiscard]] bool isCall() const { return isCall_; }
  [[nodiscard]] bool isJump() const { return isJump_; }
  [[nodiscard]] bool isConditional() const { return isConditional_; }
  [[nodiscard]] bool isTrap() const { return isTrap_; }
  [[nodiscard]] bool hasIndirectTarget() const { return hasIndirectTarget_; }

  // Setters - the disassembler fills these in after initial decode
  void setBlockStart(bool v) { isBlockStart_ = v; }
  void setFunctionStart(bool v) { isFunctionStart_ = v; }
  void setPrologue(bool v) { isPrologue_ = v; }

  void addBranch(std::uint64_t addr) { branches_.insert(addr); }
  void addCallee(std::uint64_t addr) { callees_.insert(addr); }
  void setEdges(std::size_t n) { edges_ = n; }

  void setPattern(std::string p) { pattern_ = std::move(p); }
  void setChromosomeMask(std::vector<std::uint8_t> m) {
    chromosomeMask_ = std::move(m);
  }

  Instruction(std::uint64_t address, std::uint8_t size, std::string mnemonic,
              std::string operands, std::vector<std::uint8_t> bytes,
              bool isReturn, bool isCall, bool isJump, bool isConditional,
              bool isTrap, bool hasIndirectTarget);

  // Declared here so the compiler does not try to destroy the incomplete Impl
  // type in an inline destructor.  Defined in Instruction.cpp after Impl is
  // fully defined via InstructionImpl.hpp.
  ~Instruction();
  Instruction(const Instruction &);
  Instruction &operator=(const Instruction &);
  Instruction(Instruction &&) noexcept;
  Instruction &operator=(Instruction &&) noexcept;

  private:
  /// Opaque LLVM state (MCInst and related data).
  /// Null for instructions that failed to decode.
  /// Internal code can access the full definition by including InstructionImpl.hpp.
  struct Impl;
  std::unique_ptr<Impl> impl_;

  /// Disassembler creates and populates Instruction objects, including impl_.
  friend class Disassembler;

  std::uint64_t address_{};
  std::uint8_t size_{};

  std::string mnemonic_;
  std::string operands_;
  std::string disassembly_;
  std::string pattern_;

  std::vector<std::uint8_t> bytes_;
  std::vector<std::uint8_t> chromosomeMask_;

  std::set<std::uint64_t> branches_;
  std::set<std::uint64_t> callees_;
  std::size_t edges_{};

  bool isBlockStart_{};
  bool isFunctionStart_{};
  bool isPrologue_{};
  bool isReturn_{};
  bool isCall_{};
  bool isJump_{};
  bool isConditional_{};
  bool isTrap_{};
  bool hasIndirectTarget_{};
};

} // namespace dragon
