#include <dragon/disassembler/Function.hpp>
#include <dragon/disassembler/Graph.hpp>
#include <set>

dragon::Function::Function(const Graph *graph, std::uint64_t startAddress) :
    graph_(graph), startAddress_(startAddress), valid_(false) {
  if (!graph_ || !graph_->functions_q.is_valid(startAddress_))
    return;

  std::set<std::uint64_t> queue;
  std::set<std::uint64_t> processed;
  queue.insert(startAddress_);

  while (!queue.empty()) {
    std::uint64_t blockAddr = *queue.begin();
    queue.erase(queue.begin());
    processed.insert(blockAddr);

    if (graph_->blocks_q.is_invalid(blockAddr))
      return; // bad block inside the function — bail entirely
    // generate a new block as a starting address and
    // the same graph object
    BasicBlock block{graph_, blockAddr};
    if (!block.isValid())
      continue;
    // Add successors to add them to the function
    for (std::uint64_t succ: block.successors()) {
      if (!processed.count(succ) && !graph_->functions_q.is_valid(succ))
        queue.insert(succ);
    }

    blocks_.emplace(blockAddr, std::move(block));
  }

  if (blocks_.empty())
    return;

  valid_ = true;
}

const dragon::BasicBlock *dragon::Function::block(std::uint64_t address) const {
  auto it = blocks_.find(address);
  return it != blocks_.end() ? &it->second : nullptr;
}

std::vector<const dragon::Instruction *>
dragon::Function::instructions() const {
  std::vector<const Instruction *> result;
  // Each block provides access to its instructions
  for (const auto &[addr, bb]: blocks_) {
    auto insns = bb.instructions();
    result.insert(result.end(), insns.begin(), insns.end());
  }
  return result;
}

std::size_t dragon::Function::numInstructions() const {
  std::size_t total = 0;
  for (const auto &[addr, bb]: blocks_)
    total += bb.size();
  return total;
}

std::size_t dragon::Function::edges() const {
  std::size_t total = 0;
  for (const auto &[addr, bb]: blocks_) {
    const Instruction *term = bb.terminator();
    if (term)
      total += term->edges();
  }
  return total;
}

std::size_t dragon::Function::byteSize() const {
  std::size_t total = 0;
  for (const auto &[addr, bb]: blocks_)
    total += bb.byteSize();
  return total;
}

std::size_t dragon::Function::cyclomaticComplexity() const {
  return edges() - blocks_.size() + 2;
}
