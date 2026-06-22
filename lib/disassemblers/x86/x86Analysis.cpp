#include "disassemblers/x86/x86Analysis.hpp"
#include "disassemblers/x86/X86IGNILLifter.hpp"

// Public headers
#include <dragon/analysis/BackwardSlicer.hpp>
#include <dragon/analysis/SliceExpr.hpp>
#include <dragon/analysis/SwitchDetector.hpp>
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
  logger_ = internal::Logger{configuration_.logging};
  x86IgnilLifter_ = std::make_unique<X86IGNILLifter>(logger_, architecture,
                                                     instrInfo, regInfo);
  x86Info_ = (architecture == Architecture::X86_32) ? &x86Info32 : &x86Info64;
}

dragon::x86::X86Analysis::~X86Analysis() = default;

// =============================================================================
// Public entry point
// =============================================================================

bool dragon::x86::X86Analysis::analyze(std::span<const std::uint8_t> bytes,
                                       std::uint64_t baseAddr,
                                       Graph &graph) const {
  std::unordered_set<std::uint64_t> blockStarts;
  std::unordered_set<std::uint64_t> fnStarts;

  std::vector<Instruction> instructions =
          disassembler_.disassemble(bytes, baseAddr);

  discoverBlockStarts(instructions, blockStarts);
  discoverFunctionStarts(instructions, blockStarts, fnStarts);

  for (auto &insn: instructions)
    graph.addInstruction(std::move(insn));

  bool changed = true;
  while (changed) {
    changed = false;
    commitPending(graph, blockStarts, fnStarts);
    liftPendingBlocks(graph);
    if (resolveIndirectJumps(graph, bytes, baseAddr, blockStarts, fnStarts))
      changed = true;
  }

  return true;
}

// =============================================================================
// CFG seed discovery
// =============================================================================

void dragon::x86::X86Analysis::discoverBlockStarts(
        std::vector<Instruction> &instructions,
        std::unordered_set<std::uint64_t> &basicBlockStarts) const {
  if (instructions.empty())
    return;

  basicBlockStarts.insert(instructions[0].address());

  for (const auto &I: instructions) {
    if (I.hasIndirectTarget()) {
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

  for (auto &I: instructions) {
    if (basicBlockStarts.contains(I.address()))
      I.setBlockStart(true);
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

  for (size_t i = 0, e = instructions.size(); i < e; ++i) {
    const size_t windowSize = std::min(e - i, size_t{3});
    const Instruction *window[3] = {};
    for (size_t j = 0; j < windowSize; ++j)
      window[j] = &instructions[i + j];
    if (size_t matched =
                x86Info_->isCommonFunctionPrologue({window, windowSize})) {
      std::uint64_t addr = instructions[i].address();
      functionStarts.insert(addr);
      basicBlockStarts.insert(addr);
      instructions[i].setFunctionStart(true);
      instructions[i].setBlockStart(true);
      instructions[i].setPrologue(true);
      logger_.log("Discovered function start via prologue at address 0x{:x}",
                  addr);
      i += matched - 1;
    }
  }
}

// =============================================================================
// Fixpoint helpers
// =============================================================================

void dragon::x86::X86Analysis::commitPending(
        Graph &graph, const std::unordered_set<std::uint64_t> &blockStarts,
        const std::unordered_set<std::uint64_t> &fnStarts) const {
  for (std::uint64_t addr: blockStarts)
    graph.addBlock(addr);
  for (std::uint64_t addr: fnStarts)
    graph.addFunction(addr);
}

void dragon::x86::X86Analysis::liftPendingBlocks(Graph &graph) const {
  for (BasicBlock &block: graph.blocks()) {
    if (graph.getIGNILBlock(block.startAddress()))
      continue;
    auto ib = x86IgnilLifter_->liftBlock(block, graph);
    logger_.log("Lifted block @ 0x{:x} -> {} IGNIL ops", block.startAddress(),
                ib.size());
    graph.addIGNILBlock(std::move(ib));
  }
}

std::vector<std::uint64_t>
dragon::x86::X86Analysis::collectTargets(const slice::Expr &result,
                                         std::uint64_t blockAddr) const {
  if (const auto *c = std::get_if<slice::Const>(&result.data))
    return {c->value};

#ifdef DRAGON_WITH_Z3
  if (configuration_.useZ3ForPathDiscovery) {
    z3::context ctx;
    auto targets = slice::enumeratedValues(result, ctx);
    logger_.log(
            "Z3 enumerated {} candidate target(s) for indirect jump @ 0x{:x}",
            targets.size(), blockAddr);
    return targets;
  }
#else
  // just to avoid warnings
  (void) blockAddr;
#endif

  return {};
}

bool dragon::x86::X86Analysis::integrateNewBlock(
        std::uint64_t target, std::span<const std::uint8_t> bytes,
        std::uint64_t baseAddr, Graph &graph,
        std::unordered_set<std::uint64_t> &blockStarts,
        std::unordered_set<std::uint64_t> &fnStarts) const {
  if (graph.get(target))
    return false;

  // Try to retrieve new blocks with the disassembler
  auto newInstrs = disassembler_.disassembleBlock(bytes, baseAddr, target);
  if (newInstrs.empty())
    return false;

  // This will lead to new blocks, but could be that it takes to new
  // functions start too.
  discoverBlockStarts(newInstrs, blockStarts);
  discoverFunctionStarts(newInstrs, blockStarts, fnStarts);
  // finally add instructions to the graph
  for (auto &insn: newInstrs) {
    if (!graph.get(insn.address()))
      graph.addInstruction(std::move(insn));
  }
  return true;
}

bool dragon::x86::X86Analysis::resolveIndirectJumps(
        Graph &graph, std::span<const std::uint8_t> bytes,
        std::uint64_t baseAddr, std::unordered_set<std::uint64_t> &blockStarts,
        std::unordered_set<std::uint64_t> &fnStarts) const {

  bool changed = false;

  for (const auto &block: graph.blocks()) {
    const auto *ignilBlock = graph.getIGNILBlock(block.startAddress());
    if (!ignilBlock || ignilBlock->ops().empty())
      continue;

    const auto &last = ignilBlock->ops().back();
    if (last.opcode != ignil::Opcode::JMP || last.srcs.empty() ||
        !std::holds_alternative<ignil::Reg>(last.srcs[0]))
      continue;

    const auto &reg = std::get<ignil::Reg>(last.srcs[0]);
    BackwardSlicer slicer{5};
    slice::Expr result =
            slicer.slice(ignil::Value{reg}, ignilBlock->startAddress(), graph);

    std::vector<std::uint64_t> targets;
    if (auto sw = slice::detectPossibleSwitch(result, block.startAddress(),
                                              graph)) {
      if (sw->numCases > 0) {
        logger_.log("Switch at 0x{:x}: table=0x{:x} entries={} entrySize={}",
                    block.startAddress(), sw->tableBase, sw->numCases,
                    sw->entrySize);
        targets = slice::readTableTargets(*sw, bytes, baseAddr);
      } else {
        logger_.log(
                "Switch at 0x{:x}: table=0x{:x} (bounds unknown, falling back)",
                block.startAddress(), sw->tableBase);
        targets = collectTargets(result, block.startAddress());
      }
    } else {
      targets = collectTargets(result, block.startAddress());
    }

    for (std::uint64_t target: targets) {
      if (target < baseAddr || target >= baseAddr + bytes.size()) {
        logger_.log("Resolved target 0x{:x} is out of range, skipping", target);
        continue;
      }

      logger_.log("Resolved indirect jump @ 0x{:x} -> 0x{:x}",
                  block.startAddress(), target);

      if (blockStarts.insert(target).second)
        changed = true;

      if (integrateNewBlock(target, bytes, baseAddr, graph, blockStarts,
                            fnStarts))
        changed = true;
    }
  }

  return changed;
}
