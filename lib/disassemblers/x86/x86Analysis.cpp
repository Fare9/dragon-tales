#include "disassemblers/x86/x86Analysis.hpp"
#include "disassemblers/x86/X86IGNILLifter.hpp"

// Public headers
#include <dragon/analysis/BackwardSlicer.hpp>
#include <dragon/analysis/SliceExpr.hpp>
#include <dragon/disassembler/Disassembler.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <dragon/disassembler/Instruction.hpp>
#include <dragon/disassembler/x86/x86Info.hpp>

#ifdef DRAGON_WITH_Z3
#include <dragon/analysis/ValueEnumerator.hpp>
#endif

#include <unordered_set>
#include <vector>

dragon::x86::X86Analysis::X86Analysis(Architecture architecture,
                                      const Disassembler &disassembler,
                                      const Configuration &configuration,
                                      const llvm::MCInstrInfo &instrInfo,
                                      const llvm::MCRegisterInfo &regInfo) :
    architecture_(architecture), disassembler_(disassembler),
    configuration_(configuration), instrInfo_(instrInfo), regInfo_(regInfo) {
  static const X86Info x86Info32{Architecture::X86_32, instrInfo, regInfo};
  static const X86Info x86Info64{Architecture::X86_64, instrInfo, regInfo};
  x86IgnilLifter_ =
          std::make_unique<X86IGNILLifter>(architecture, instrInfo, regInfo);
  x86Info_ = (architecture == Architecture::X86_32) ? &x86Info32 : &x86Info64;
  logger_ = internal::Logger{configuration_.logging};
}

dragon::x86::X86Analysis::~X86Analysis() = default;

bool dragon::x86::X86Analysis::analyze(std::span<const std::uint8_t> bytes,
                                       std::uint64_t baseAddr,
                                       Graph &graph) const {
  std::unordered_set<std::uint64_t> basicBlockStarts;
  std::unordered_set<std::uint64_t> functionStarts;

  std::vector<Instruction> instructions =
          disassembler_.disassemble(bytes, baseAddr);

  discoverBlockStarts(instructions, basicBlockStarts);

  discoverFunctionStarts(instructions, basicBlockStarts, functionStarts);

  for (auto &insn: instructions)
    graph.addInstruction(std::move(insn));

  for (std::uint64_t addr: basicBlockStarts)
    graph.addBlock(addr);

  for (std::uint64_t addr: functionStarts)
    graph.addFunction(addr);

  // Fixpoint loop
  // Each iteration:
  //   1. Commit pending block/function starts to the graph
  //   2. Lift IGNIL for any block that doesn't have it yet
  //   3. Resolve indirect JMPs via backward slicing
  //      ---> if a resolved target is new, add it and loop again
  bool changed = true;
  while (changed) {
    changed = false;

    for (std::uint64_t addr: basicBlockStarts)
      graph.addBlock(addr);
    for (std::uint64_t addr: functionStarts)
      graph.addFunction(addr);

    for (BasicBlock &block: graph.blocks()) {
      if (graph.getIGNILBlock(block.startAddress()))
        continue;
      auto ib = x86IgnilLifter_->liftBlock(block, graph);
      logger_.log("Lifted block @ 0x{:x} -> {} IGNIL ops", block.startAddress(),
                  ib.size());
      graph.addIGNILBlock(std::move(ib));
    }

    // Go over the ignil blocks, here we will do the analyses
    for (const auto &block: graph.blocks()) {
      const auto *ignilBlock = graph.getIGNILBlock(block.startAddress());
      if (!ignilBlock || ignilBlock->ops().empty())
        continue;

      // We are looking for jump instructions
      // pointed with a register, we try to
      // resolve it to a constant
      const auto &last = ignilBlock->ops().back();
      // JMP REG, the jumps to memory will be generated
      // as a jump to a register
      if (last.opcode != ignil::Opcode::JMP || last.srcs.empty() ||
          !std::holds_alternative<ignil::Reg>(last.srcs[0]))
        continue;

      // Backward slicing algorithm, it will just go back in the basic blocks
      // looking for the instruction generating the value
      const auto &reg = std::get<ignil::Reg>(last.srcs[0]);

      // Need from backward slicing, maximum of 5 blocks behind for the analysis
      BackwardSlicer slicer{5};
      slice::Expr result = slicer.slice(ignil::Value{reg},
                                        ignilBlock->startAddress(), graph);

      // Collect candidate jump targets.
      // Fast path: slicer resolved the register to a single constant.
      std::vector<std::uint64_t> targets;
      if (const auto *c = std::get_if<slice::Const>(&result.data)) {
        targets.push_back(c->value);
      }
#ifdef DRAGON_WITH_Z3
      // Slow path: ask Z3 to enumerate all satisfying values.
      else if (configuration_.useZ3ForPathDiscovery) {
        z3::context ctx;
        // we use the utility enumeratedValues, just provide with the expression
        // result, and the z3 context
        targets = slice::enumeratedValues(result, ctx);
        logger_.log("Z3 enumerated {} candidate target(s) for indirect jump @ "
                    "0x{:x}",
                    targets.size(), block.startAddress());
      }
#endif

      if (targets.empty())
        continue;

      for (std::uint64_t target: targets) {
        if (target < baseAddr || target >= baseAddr + bytes.size()) {
          logger_.log("Resolved target 0x{:x} is out of range, skipping",
                      target);
          continue;
        }

        logger_.log("Resolved indirect jump @ 0x{:x} -> 0x{:x}",
                    block.startAddress(), target);

        if (basicBlockStarts.insert(target).second)
          changed = true;

        // If instructions at the target are not yet in the graph, disassemble
        // the block starting there and integrate the results so the next
        // iteration can lift and analyze it.
        if (!graph.get(target)) {
          auto newInstrs =
                  disassembler_.disassembleBlock(bytes, baseAddr, target);
          if (!newInstrs.empty()) {
            discoverBlockStarts(newInstrs, basicBlockStarts);
            discoverFunctionStarts(newInstrs, basicBlockStarts, functionStarts);
            for (auto &insn: newInstrs) {
              if (!graph.get(insn.address()))
                graph.addInstruction(std::move(insn));
            }
            changed = true;
          }
        }
      }
    }
  }

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
        logger_.log("Discovered callee at address 0x{:x}", callee);
      }
    }
  }

  for (size_t i = 0, e = instructions.size(); i + 1 < e; ++i) {
    const Instruction *window[2] = {&instructions[i], &instructions[i + 1]};
    if (x86Info_->isCommonFunctionPrologue({window, 2})) {
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
