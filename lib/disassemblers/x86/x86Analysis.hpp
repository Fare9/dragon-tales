#pragma once

#include <span>
#include <unordered_set>
#include <vector>

#include <dragon/Architecture.hpp>
#include <dragon/Configuration.hpp>
#include "utils/Logger.hpp"

#include <llvm/MC/MCInstrInfo.h>
#include <llvm/MC/MCRegisterInfo.h>

namespace dragon {

class Instruction;
class Disassembler;
class Graph;
class X86Info;

namespace x86 {

class X86Analysis {
  public:
  /// Constructor for x86Analysis, we need to understand what architecture
  /// we are working with, the disassembler to retrieve all the instructions
  /// and the configuration for logging.
  X86Analysis(Architecture architecture, const Disassembler &disassembler,
              const Configuration &configuration,
                  const llvm::MCInstrInfo &instrInfo,
                  const llvm::MCRegisterInfo &regInfo);
  /// Populate the graph received with the decoded
  /// instructions, blocks and functions.
  /// @param bytes bytes from instructions to be decoded.
  /// @param baseAddr base address used to decode the instructions.
  /// @param graph graph where data will be populated.
  bool analyze(std::span<const std::uint8_t> bytes, std::uint64_t baseAddr,
               Graph &graph) const;

  private:
  /// We apply a simple analysis to retrieve possible block starts,
  /// we start from the first instruction retrieved by the disassembler
  /// looking for jump instructions, the next instructions and the
  /// jump address are marked as start of blocks.
  /// @param instructions the instructions that were disassembled.
  /// @param basicBlockStarts the set where to store the addresses of the basic
  /// blocks.
  void discoverBlockStarts(
          std::vector<Instruction> &instructions,
          std::unordered_set<std::uint64_t> &basicBlockStarts) const;

  /// We apply a simple analysis to retrieve the beginning of the functions
  /// we follow calls to look for functions, those points will be marked
  /// as function beginnings and as basic blocks too.
  /// @param instructions the instructions that were disassembled.
  /// @param basicBlockStarts the set where to store the addresses of the basic
  /// blocks.
  /// @param functionStarts the set where to store the addresses of the
  /// functions.
  void discoverFunctionStarts(
          std::vector<Instruction> &instructions,
          std::unordered_set<std::uint64_t> &basicBlockStarts,
          std::unordered_set<std::uint64_t> &functionStarts) const;

  Architecture architecture_;
  const Disassembler &disassembler_;
  const Configuration &configuration_;
  const llvm::MCInstrInfo &instrInfo_;
  const llvm::MCRegisterInfo &regInfo_;
  const X86Info *x86Info_;
  internal::Logger logger_;
};

} // namespace x86
} // namespace dragon
