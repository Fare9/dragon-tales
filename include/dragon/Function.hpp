#pragma once

#include <cstddef>
#include <cstdint>
#include <dragon/BasicBlock.hpp>
#include <map>
#include <vector>

namespace dragon {

// Forward declarations
class Graph;
class Instruction;

/// Function is a viewer for the instructions that internally contains
/// its BasicBlocks, same as the BasicBlock class, it contains a pointer
/// for the Graph that will be used to retrieve the instructions.
class Function {
  public:
  // Constructs a view over the function starting at startAddress.
  // BFS over blocks via their successors, collects all reachable blocks.
  // Sets valid_ = false if address is not in functions_q.valid or has no
  // blocks.
  Function(const Graph *graph, std::uint64_t startAddress);

  [[nodiscard]] bool isValid() const { return valid_; }

  [[nodiscard]] std::uint64_t startAddress() const { return startAddress_; }

  // All blocks in this function, keyed by start address.
  [[nodiscard]] const std::map<std::uint64_t, BasicBlock> &blocks() const {
    return blocks_;
  }

  // Lookup a single block by address.
  [[nodiscard]] const BasicBlock *block(std::uint64_t address) const;

  // All instructions across all blocks in address order.
  [[nodiscard]] std::vector<const Instruction *> instructions() const;

  [[nodiscard]] std::size_t numBlocks() const { return blocks_.size(); }
  [[nodiscard]] std::size_t numInstructions() const;
  [[nodiscard]] std::size_t edges() const;
  [[nodiscard]] std::size_t byteSize() const;
  [[nodiscard]] std::size_t cyclomaticComplexity() const;

  [[nodiscard]] bool empty() const { return blocks_.empty(); }

  private:
  const Graph *graph_;
  std::uint64_t startAddress_;
  std::map<std::uint64_t, BasicBlock> blocks_;
  bool valid_;
};

} // namespace dragon
