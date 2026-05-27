#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

namespace dragon {

// Forward declarations, full types only needed in BasicBlock.cpp
class Graph;
class Instruction;

/// A BasicBlock will contain references to where it instructions are
/// it will not hold ownership of the instructions, in that way it will
/// make easier having different basic blocks, invalidate them, add
/// instructions to a function, or removing them, and just update addresses.
/// The BasicBlock receives a pointer to the graph so it can retrieve the
/// instructions to provide them to the user.
class BasicBlock {
  public:
  /// Constructs a view over the block starting at startAddress.
  /// Walks graph listing to find the terminator. Sets valid_ = false if
  /// the address is not in blocks_q.valid or the block is non-contiguous.
  BasicBlock(const Graph *graph, std::uint64_t startAddress);

  [[nodiscard]] bool isValid() const { return valid_; }

  [[nodiscard]] std::uint64_t startAddress() const { return startAddress_; }
  [[nodiscard]] std::uint64_t terminatorAddress() const {
    return terminatorAddress_;
  }

  // Walk listing from startAddress to terminator (inclusive).
  [[nodiscard]] std::vector<const Instruction *> instructions() const;

  // Lookup a single instruction within this block by address.
  [[nodiscard]] const Instruction *get(std::uint64_t address) const;

  // Terminator instruction (the last one in the block).
  [[nodiscard]] const Instruction *terminator() const;

  [[nodiscard]] std::size_t size() const; // instruction count
  [[nodiscard]] std::size_t byteSize() const; // total byte size
  [[nodiscard]] bool empty() const;

  // For the successors of current block
  [[nodiscard]] std::set<std::uint64_t> successors() const;

  private:
  const Graph *graph_;
  std::uint64_t startAddress_;
  std::uint64_t terminatorAddress_;
  bool valid_;
};

} // namespace dragon
