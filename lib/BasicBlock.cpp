#include <dragon/disassembler/BasicBlock.hpp>
#include <dragon/disassembler/Graph.hpp>

dragon::BasicBlock::BasicBlock(const Graph *graph, std::uint64_t startAddress) :
    graph_(graph), startAddress_(startAddress), terminatorAddress_(0),
    valid_(false) {
  if (!graph_ || !graph_->blocks_q.is_valid(startAddress_))
    return;

  std::uint64_t expectedNext = 0;
  bool hasPrev = false;
  const Instruction *prevInsn = nullptr;

  for (auto it = graph_->listing().lower_bound(startAddress_);
       it != graph_->listing().end(); ++it) {
    const Instruction &insn = it->second;

    // Gap between instructions - block is non-contiguous
    if (hasPrev && insn.address() != expectedNext)
      return;

    // Next block starts here (not the entry) — previous insn is the terminator
    if (insn.address() != startAddress_ && insn.isBlockStart()) {
      if (!prevInsn)
        return;
      terminatorAddress_ = prevInsn->address();
      valid_ = true;
      return;
    }

    expectedNext = insn.address() + insn.size();
    hasPrev = true;

    if (insn.isJump() || insn.isTrap() || insn.isReturn()) {
      terminatorAddress_ = insn.address();
      valid_ = true;
      return;
    }

    prevInsn = &insn;
  }
  // Fell off the end of the listing without finding a terminator - stays
  // invalid
}

std::vector<const dragon::Instruction *>
dragon::BasicBlock::instructions() const {
  std::vector<const Instruction *> result;
  if (!valid_)
    return result;

  for (auto it = graph_->listing().lower_bound(startAddress_);
       it != graph_->listing().end(); ++it) {
    result.push_back(&it->second);
    if (it->first == terminatorAddress_)
      break;
  }
  return result;
}

const dragon::Instruction *
dragon::BasicBlock::get(std::uint64_t address) const {
  if (!valid_ || address < startAddress_ || address > terminatorAddress_)
    return nullptr;
  return graph_->get(address);
}

const dragon::Instruction *dragon::BasicBlock::terminator() const {
  if (!valid_)
    return nullptr;
  return graph_->get(terminatorAddress_);
}

std::set<std::uint64_t> dragon::BasicBlock::successors() const {
  const Instruction *term = terminator();
  if (!term || term->isReturn() || term->isTrap())
    return {};
  if (term->isJump() && term->isConditional()) {
    auto succs = term->branches();
    succs.insert(term->address() + term->size());
    return succs;
  }
  if (!term->branches().empty())
    return term->branches();
  // Non-jump block end: fall-through is the instruction immediately after
  return {term->address() + term->size()};
}

std::size_t dragon::BasicBlock::size() const { return instructions().size(); }

std::size_t dragon::BasicBlock::byteSize() const {
  if (!valid_)
    return 0;
  const Instruction *term = terminator();
  if (!term)
    return 0;
  return (term->address() + term->size()) - startAddress_;
}

bool dragon::BasicBlock::empty() const { return !valid_; }
