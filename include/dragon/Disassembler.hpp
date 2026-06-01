#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <dragon/Architecture.hpp>
#include <dragon/Configuration.hpp>
#include <dragon/Graph.hpp>
#include <dragon/Instruction.hpp>

namespace dragon {

/// A disassembler for various architectures supported by LLVM
/// the disassembler will work as a simple tool, so we can provide
/// different inputs, which are mostly an array of bytes, and the
/// base address to take into account for the disassembly process.
/// Internally, it will use different disassembly algorithms for
/// doing the processing of the disassembly of the instructions.
///
/// At the moment we provide a Graph, the disassembler can make
/// use of this graph class to generate functions from the disassembled
/// code. At that moment, once we call its disassemble function
/// the Graph will be populated with the data, and the user can
/// make use of it. Also, the user can ask to disassemble the data
/// and retrieve a function from an address.
class Disassembler {
  public:
  /// Constructor for the disassembler, we need to provide an
  /// architecture that will internally be used to disassemble
  /// the bytes using LLVM disassembler. The configuration will
  /// contain optional values for the output format.
  Disassembler(Architecture architecture, const Configuration &configuration);
  ~Disassembler();

  Disassembler(const Disassembler &) = delete;
  Disassembler &operator=(const Disassembler &) = delete;

  Disassembler(Disassembler &&) noexcept;
  Disassembler &operator=(Disassembler &&) noexcept = default;

  [[nodiscard]] bool isValid() const noexcept;
  [[nodiscard]] Architecture architecture() const noexcept {
    return architecture_;
  }
  [[nodiscard]] const Configuration &configuration() const noexcept {
    return configuration_;
  }

  /// disassemble will receive as parameters a list of bytes that
  /// represent the instructions from code, or a .text section, etc.
  /// baseAddress is used to generate the instructions and to take
  /// into account things like jumps, and calls, etc.
  [[nodiscard]] std::vector<Instruction>
  disassemble(std::span<const std::uint8_t> bytes,
              std::uint64_t baseAddress) const;

  void disassemble(std::span<const std::uint8_t> bytes,
                   std::uint64_t baseAddress, Graph &graph) const;

          private : struct Impl;

  Instruction decodeOne(std::span<const std::uint8_t> bytes,
                        std::uint64_t address, std::size_t &offset) const;

  [[nodiscard]] std::vector<Instruction>
  linearSweep(std::span<const std::uint8_t> bytes,
              std::uint64_t baseAddress) const;

  [[nodiscard]] std::vector<Instruction>
  recursiveTraversal(std::span<const std::uint8_t> bytes,
                     std::uint64_t baseAddress) const;

  Architecture architecture_;
  Configuration configuration_;
  std::unique_ptr<Impl> impl_;
};

} // namespace dragon
