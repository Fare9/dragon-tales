#include "disassemblers/x86/x86Analysis.hpp"

// Public headers
#include <dragon/BasicBlock.hpp>
#include <dragon/Disassembler.hpp>
#include <dragon/Function.hpp>
#include <dragon/Graph.hpp>
#include <dragon/Instruction.hpp>
#include <dragon/x86Info.hpp>

// Private headers
#include "InstructionImpl.hpp"

#include <unordered_set>
#include <vector>

dragon::x86::X86Analysis::X86Analysis(Architecture architecture,
                                      const Disassembler &disassembler,
                                      const Configuration &configuration) :
    architecture_(architecture), disassembler_(disassembler),
    configuration_(configuration) {
  logger_ = internal::Logger{configuration_.logging};
}

bool dragon::x86::X86Analysis::analyze(std::span<const std::uint8_t> bytes,
                                       std::uint64_t baseAddr, Graph &graph) {
  std::unordered_set<std::uint64_t> basicBlockStarts;
  std::unordered_set<std::uint64_t> functionStarts;
  std::vector<Instruction> instructions;

  instructions = disassembler_.disassemble(bytes, baseAddr);

  discoverBlockStarts(instructions, basicBlockStarts);

  discoverFunctionStarts(instructions, basicBlockStarts, functionStarts);

  for (auto &insn: instructions)
    graph.addInstruction(std::move(insn));

  for (std::uint64_t addr: basicBlockStarts)
    graph.addBlock(addr);

  for (std::uint64_t addr: functionStarts)
    graph.addFunction(addr);

  return true;
}


void dragon::x86::X86Analysis::discoverBlockStarts(
        std::vector<Instruction> &instructions,
        std::unordered_set<std::uint64_t> &basicBlockStarts) const {
  if (instructions.empty())
    return;

  basicBlockStarts.insert(instructions[0].address());

  for (const auto &I: instructions) {

    if (I.hasIndirectTarget()) {
      // For the moment, not possible to retrieve an indirect target
      // we'll have to think about a way to do it
      logger_.log("Instruction {} at address 0x{:x} has an indirect target.",
                  I.disassembly(), I.address());
    }

    if (I.isJump()) {
      for (auto addr: I.branches()) {
        basicBlockStarts.insert(addr);
        logger_.log("Discovered 0x{:x} as beginning of block", addr);
      }
      if (I.isConditional()) {
        std::uint64_t addr = I.address() + I.size();
        basicBlockStarts.insert(addr);
        logger_.log("Discovered 0x{:x} as beginning of block", addr);
      }
    }
  }
  // Set some information about the instructions
  for (auto &I: instructions) {
    // For those instructions that we found,
    if (basicBlockStarts.contains(I.address())) {
      I.setBlockStart(true);
    }
  }
}

void dragon::x86::X86Analysis::discoverFunctionStarts(
        std::vector<Instruction> &instructions,
        std::unordered_set<std::uint64_t> &basicBlockStarts,
        std::unordered_set<std::uint64_t> &functionStarts) const {
  for (auto &I: instructions) {
    if (I.isCall()) {
      for (auto callee: I.callees()) {
        functionStarts.insert(callee);
        basicBlockStarts.insert(callee);
        I.setFunctionStart(true);
        I.setBlockStart(true);
        logger_.log("Discovered callee at address 0x{:x}", callee);
      }
    }
  }

  // We need information about the instructions.
  const X86Info x86Info{architecture_};
  for (size_t i = 0, e = instructions.size(); i + 1 < e; ++i) {
    const Instruction *window[2] = {&instructions[i], &instructions[i + 1]};
    if (x86Info.isCommonFunctionPrologue({window, 2})) {
      std::uint64_t addr = instructions[i].address();
      functionStarts.insert(addr);
      basicBlockStarts.insert(addr);
      instructions[i].setFunctionStart(true);
      instructions[i].setBlockStart(true);
      instructions[i].setPrologue(true);
      logger_.log("Discovered function start via prologue at address 0x{:x}",
                  addr);
    }
  }
}
