#include <dragon/Graph.hpp>
#include <utility>

// -- GraphQueue

void dragon::GraphQueue::enqueue(const std::uint64_t addr) {
  if (processed_.contains(addr) || pending_.contains(addr))
    return;
  pending_.insert(addr);
  queue_.insert(addr);
}

bool dragon::GraphQueue::is_queued(const std::uint64_t addr) const {
  return queue_.contains(addr);
}
bool dragon::GraphQueue::is_pending(const std::uint64_t addr) const {
  return pending_.contains(addr);
}
bool dragon::GraphQueue::is_processed(const std::uint64_t addr) const {
  return processed_.contains(addr);
}
bool dragon::GraphQueue::is_valid(const std::uint64_t addr) const {
  return valid_.contains(addr);
}
bool dragon::GraphQueue::is_invalid(const std::uint64_t addr) const {
  return invalid_.contains(addr);
}
bool dragon::GraphQueue::empty() const { return queue_.empty(); }

std::optional<std::uint64_t> dragon::GraphQueue::next() {
  if (queue_.empty())
    return std::nullopt;
  const auto it = queue_.begin();
  std::uint64_t addr = *it;
  queue_.erase(it);
  pending_.erase(addr);
  processed_.insert(addr);
  return addr;
}

void dragon::GraphQueue::mark_processed(std::uint64_t addr) {
  processed_.insert(addr);
}

void dragon::GraphQueue::mark_valid(std::uint64_t addr) {
  if (processed_.contains(addr)) // add to valid only if previously processed
    valid_.insert(addr);
}

void dragon::GraphQueue::mark_invalid(std::uint64_t addr) {
  if (!valid_.contains(addr))
    invalid_.insert(addr);
}

void dragon::GraphQueue::enqueue_extend(const std::set<std::uint64_t> &addrs) {
  for (const std::uint64_t addr: addrs)
    enqueue(addr);
}

// -- Graph
dragon::Graph::Graph(Architecture architecture, Configuration config) :
    architecture_(architecture), configuration_(std::move(config)) {}

void dragon::Graph::addInstruction(Instruction insn) {
  std::uint64_t addr = insn.address();
  listing_.emplace(addr, std::move(insn));
  instructions_q.mark_processed(addr);
  instructions_q.mark_valid(addr);
}

void dragon::Graph::addBlock(std::uint64_t addr) {
  blocks_q.mark_processed(addr);
  blocks_q.mark_valid(addr);
}

void dragon::Graph::addFunction(std::uint64_t addr) {
  functions_q.mark_processed(addr);
  functions_q.mark_valid(addr);
}

void dragon::Graph::insert(Instruction insn) {
  std::uint64_t addr = insn.address();
  listing_.emplace(addr, std::move(insn));;
}

void dragon::Graph::update(const Instruction &insn) {
  if (auto it = listing_.find(insn.address()); it != listing_.end())
    it->second = insn;
}

const dragon::Instruction *dragon::Graph::get(std::uint64_t address) const {
  const auto it = listing_.find(address);
  return it != listing_.end() ? &it->second : nullptr;
}

dragon::Instruction *dragon::Graph::get(std::uint64_t address) {
  const auto it = listing_.find(address);
  return it != listing_.end() ? &it->second : nullptr;
}

std::vector<dragon::Instruction *> dragon::Graph::instructions() {
  std::vector<Instruction *> result;
  for (std::uint64_t addr: instructions_q.valid()) {
    if (auto *insn = get(addr))
      result.push_back(insn);
  }
  return result;
}

std::vector<dragon::BasicBlock> dragon::Graph::blocks() {
  std::vector<BasicBlock> result;
  for (std::uint64_t addr: blocks_q.valid()) {
    if (BasicBlock bb{this, addr}; bb.isValid())
      result.push_back(std::move(bb));
  }
  return result;
}

std::vector<dragon::Function> dragon::Graph::functions() {
  std::vector<Function> result;
  for (std::uint64_t addr: functions_q.valid()) {
    Function fn(this, addr);
    if (fn.isValid())
      result.push_back(std::move(fn));
  }
  return result;
}

void dragon::Graph::merge(Graph other) {
  for (auto &[addr, insn]: other.listing_)
    listing_.try_emplace(addr, std::move(insn));

  for (std::uint64_t addr: other.instructions_q.processed())
    instructions_q.mark_processed(addr);
  for (std::uint64_t addr: other.instructions_q.valid())
    instructions_q.mark_valid(addr);
  for (std::uint64_t addr: other.instructions_q.invalid())
    instructions_q.mark_invalid(addr);
  for (std::uint64_t addr: other.blocks_q.processed())
    blocks_q.mark_processed(addr);
  for (std::uint64_t addr: other.blocks_q.valid())
    blocks_q.mark_valid(addr);
  for (std::uint64_t addr: other.blocks_q.invalid())
    blocks_q.mark_invalid(addr);
  for (std::uint64_t addr: other.functions_q.processed())
    functions_q.mark_processed(addr);
  for (std::uint64_t addr: other.functions_q.valid())
    functions_q.mark_valid(addr);
  for (std::uint64_t addr: other.functions_q.invalid())
    functions_q.mark_invalid(addr);

  while (auto addr = other.instructions_q.next())
    instructions_q.enqueue(*addr);
  while (auto addr = other.blocks_q.next())
    blocks_q.enqueue(*addr);
  while (auto addr = other.functions_q.next())
    functions_q.enqueue(*addr);
}
