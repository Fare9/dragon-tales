#pragma once

#include <cstdint>
#include <dragon/Architecture.hpp>
#include <dragon/BasicBlock.hpp>
#include <dragon/Configuration.hpp>
#include <dragon/Function.hpp>
#include <dragon/Instruction.hpp>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace dragon {

struct GraphQueue {
  std::set<std::uint64_t> queue;
  std::set<std::uint64_t> pending;
  std::set<std::uint64_t> processed;
  std::set<std::uint64_t> valid;
  std::set<std::uint64_t> invalid;

  void enqueue(std::uint64_t addr);
  [[nodiscard]] bool is_queued(std::uint64_t addr) const;
  [[nodiscard]] bool is_pending(std::uint64_t addr) const;
  [[nodiscard]] bool is_processed(std::uint64_t addr) const;
  [[nodiscard]] bool is_valid(std::uint64_t addr) const;
  [[nodiscard]] bool is_invalid(std::uint64_t addr) const;
  [[nodiscard]] bool empty() const;

  std::optional<std::uint64_t> next(); // pops from queue, moves to pending
};

/// A representation of whole disassembled code, Graph will be
/// in charge of function generation. Graph also serves as the
/// container from the instructions, as Functions and Blocks are
/// only views to those instructions.
class Graph {
  public:
  Graph(Architecture architecture, const Configuration &config);

  [[nodiscard]] Architecture architecture() const { return architecture_; }
  [[nodiscard]] const Configuration &configuration() const {
    return configuration_;
  }

  void insert(Instruction insn);
  void update(const Instruction &insn);

  [[nodiscard]] const Instruction *get(std::uint64_t address) const;
  [[nodiscard]] Instruction *get(std::uint64_t address);

  [[nodiscard]] const std::map<std::uint64_t, Instruction> &listing() const {
    return listing_;
  }

  // View accessors — iterate valid sets, construct views on the fly
  [[nodiscard]] std::vector<Instruction *> instructions();
  [[nodiscard]] std::vector<BasicBlock> blocks();
  [[nodiscard]] std::vector<Function> functions();

  [[nodiscard]] std::size_t size() const { return listing_.size(); }
  [[nodiscard]] bool empty() const { return listing_.empty(); }

  void merge(Graph other);

  GraphQueue instructions_q;
  GraphQueue blocks_q;
  GraphQueue functions_q;

  private:
  Architecture architecture_;
  Configuration configuration_;
  std::map<std::uint64_t, Instruction> listing_;
};

} // namespace dragon
