#pragma once

#include <dragon/Architecture.hpp>
#include <dragon/disassembler/BasicBlock.hpp>
#include <dragon/Configuration.hpp>
#include <dragon/disassembler/Function.hpp>
#include <dragon/disassembler/Instruction.hpp>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include <dragon/lifters/ignil/IGNIL.hpp>

namespace dragon {

class GraphQueue {
  public:
  void enqueue(std::uint64_t addr);
  [[nodiscard]] bool is_queued(std::uint64_t addr) const;
  [[nodiscard]] bool is_pending(std::uint64_t addr) const;
  [[nodiscard]] bool is_processed(std::uint64_t addr) const;
  [[nodiscard]] bool is_valid(std::uint64_t addr) const;
  [[nodiscard]] bool is_invalid(std::uint64_t addr) const;
  [[nodiscard]] bool empty() const;

  std::optional<std::uint64_t> next(); // pops from queue, marks processed

  /// unconditional set as processed
  void mark_processed(std::uint64_t addr);
  /// mark as valid only if previously processed
  void mark_valid(std::uint64_t addr); // no-op if not processed
  /// mark invalid only if not already valid
  void mark_invalid(std::uint64_t addr); // no-op if already valid
  /// add addresses to queue of processing
  void enqueue_extend(const std::set<std::uint64_t> &addrs);

  [[nodiscard]] const std::set<std::uint64_t> &processed() const {
    return processed_;
  }

  [[nodiscard]] const std::set<std::uint64_t> &valid() const { return valid_; }

  [[nodiscard]] const std::set<std::uint64_t> &invalid() const {
    return invalid_;
  }

  private:
  std::set<std::uint64_t> queue_;
  std::set<std::uint64_t> pending_;
  std::set<std::uint64_t> processed_;
  std::set<std::uint64_t> valid_;
  std::set<std::uint64_t> invalid_;
};

/// A representation of whole disassembled code, Graph will be
/// in charge of function generation. Graph also serves as the
/// container from the instructions, as Functions and Blocks are
/// only views to those instructions.
class Graph {
  public:
  Graph(Architecture architecture, Configuration config);

  [[nodiscard]] Architecture architecture() const { return architecture_; }
  [[nodiscard]] const Configuration &configuration() const {
    return configuration_;
  }

  void addInstruction(Instruction insn);
  void addBlock(std::uint64_t addr);
  void addFunction(std::uint64_t addr);
  void update(const Instruction &insn);

  void addIGNILBlock(ignil::Block block);
  ignil::Block *getIGNILBlock(std::uint64_t addr);
  const ignil::Block *getIGNILBlock(std::uint64_t addr) const;

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
  void insert(Instruction insn);

  Architecture architecture_;
  Configuration configuration_;
  std::map<std::uint64_t, Instruction> listing_;
  std::map<std::uint64_t, ignil::Block> ignilBlocks_;
};

} // namespace dragon
