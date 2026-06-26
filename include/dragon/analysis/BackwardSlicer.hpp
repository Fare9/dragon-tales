#pragma once

#include <dragon/analysis/SliceExpr.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>

#include <cstdint>
#include <unordered_map>

namespace dragon {

class Graph;

/// Backward-slices an IGNIL value through a CFG to build a symbolic
/// SliceExpr tree.
///
/// The slicer walks backwards through IGNIL ops.  Within a block,
/// Temp definitions are single-assignment so tracing is trivial.
/// When a Reg value is not defined in the current block the slicer
/// follows the unique predecessor (if one exists).  Blocks with more
/// than one predecessor - join nodes - are not crossed: the Reg is
/// returned as SymReg (unknown).
///
/// The depth parameter caps the number of inter-block steps, so the
/// search terminates in bounded time even for deep chains.
class BackwardSlicer {
  public:
  explicit BackwardSlicer(int maxBlocks = 5);

  /// Slice `root` as it appears at the end of the block at `blockAddr`.
  /// Returns a SliceExpr tree suitable for Z3 lowering.
  [[nodiscard]] slice::Expr slice(const ignil::Value &root,
                                  std::uint64_t blockAddr,
                                  const Graph &graph) const;

  private:
  // Maps a block address to its unique predecessor.
  // Blocks with zero or multiple predecessors are absent (join nodes / entry).
  using PredMap = std::unordered_map<std::uint64_t, std::uint64_t>;

  [[nodiscard]] PredMap buildPredecessorMap(const Graph &graph) const;

  // Resolve `v` starting the backward scan at `beforeIdx` (exclusive) inside
  // `block`.  Crosses into predecessor blocks when `depth` > 0.
  [[nodiscard]] slice::Expr sliceValue(const ignil::Value &v,
                                       const ignil::Block &block, int beforeIdx,
                                       const Graph &graph, const PredMap &preds,
                                       int depth) const;

  // Turn the op that defines a value into a SliceExpr by recursively slicing
  // each source operand.
  [[nodiscard]] slice::Expr buildFromOp(const ignil::Op &op,
                                        const ignil::Block &block, int opIdx,
                                        const Graph &graph,
                                        const PredMap &preds, int depth) const;
  // To keep the maximum number to go backward
  // this is selected by user with the configuration.
  int maxBlocks_;
};

} // namespace dragon
