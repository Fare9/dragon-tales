#include <dragon/Graph.hpp>

// -- GraphQueue

void dragon::GraphQueue::enqueue(std::uint64_t addr) {
  if (processed.contains(addr) || pending.contains(addr))
    return;
  pending.insert(addr);
  queue.insert(addr);
}

bool dragon::GraphQueue::is_queued(std::uint64_t addr) const {
  return queue.contains(addr);
}
bool dragon::GraphQueue::is_pending(std::uint64_t addr) const {
  return pending.contains(addr);
}
bool dragon::GraphQueue::is_processed(std::uint64_t addr) const {
  return processed.contains(addr);
}
bool dragon::GraphQueue::is_valid(std::uint64_t addr) const {
  return valid.contains(addr);
}
bool dragon::GraphQueue::is_invalid(std::uint64_t addr) const {
  return invalid.contains(addr);
}
bool dragon::GraphQueue::empty() const { return queue.empty(); }

std::optional<std::uint64_t> dragon::GraphQueue::next() {
  if (queue.empty())
    return std::nullopt;
  auto it = queue.begin();
  std::uint64_t addr = *it;
  queue.erase(it);
  pending.erase(addr);
  return addr;
}

// -- Graph
dragon::Graph::Graph(Architecture architecture, const Configuration &config) :
    architecture_(architecture), configuration_(config) {}

void dragon::Graph::insert(Instruction insn) {
  std::uint64_t addr = insn.address();
  listing_.emplace(addr, std::move(insn));
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
  for (std::uint64_t addr: instructions_q.valid) {
    if (auto *insn = get(addr))
      result.push_back(insn);
  }
  return result;
}

std::vector<dragon::BasicBlock> dragon::Graph::blocks() {
  std::vector<BasicBlock> result;
  for (std::uint64_t addr: blocks_q.valid) {
    if (BasicBlock bb{this, addr}; bb.isValid())
      result.push_back(std::move(bb));
  }
  return result;
}

std::vector<dragon::Function> dragon::Graph::functions() {
  std::vector<Function> result;
  for (std::uint64_t addr: functions_q.valid) {
    Function fn(this, addr);
    if (fn.isValid())
      result.push_back(std::move(fn));
  }
  return result;
}

void dragon::Graph::merge(Graph other) {
  for (auto &[addr, insn]: other.listing_)
    listing_.try_emplace(addr, std::move(insn));

  for (std::uint64_t addr: other.instructions_q.processed)
    instructions_q.processed.insert(addr);
  for (std::uint64_t addr: other.instructions_q.valid)
    instructions_q.valid.insert(addr);
  for (std::uint64_t addr: other.instructions_q.invalid)
    instructions_q.invalid.insert(addr);
  for (std::uint64_t addr: other.blocks_q.processed)
    blocks_q.processed.insert(addr);
  for (std::uint64_t addr: other.blocks_q.valid)
    blocks_q.valid.insert(addr);
  for (std::uint64_t addr: other.blocks_q.invalid)
    blocks_q.invalid.insert(addr);
  for (std::uint64_t addr: other.functions_q.processed)
    functions_q.processed.insert(addr);
  for (std::uint64_t addr: other.functions_q.valid)
    functions_q.valid.insert(addr);
  for (std::uint64_t addr: other.functions_q.invalid)
    functions_q.invalid.insert(addr);

  while (auto addr = other.instructions_q.next())
    instructions_q.enqueue(*addr);
  while (auto addr = other.blocks_q.next())
    blocks_q.enqueue(*addr);
  while (auto addr = other.functions_q.next())
    functions_q.enqueue(*addr);
}
