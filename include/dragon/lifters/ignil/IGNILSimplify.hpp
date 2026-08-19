#pragma once

#include <dragon/lifters/ignil/IGNIL.hpp>

#include <cstddef>

namespace dragon::ignil {

/// What changed during a simplifyBlock() run. Useful for tests and future
/// coverage/audit tooling (mirrors the role
/// scripts/audit_x86_lifter_coverage.py plays for the lifter itself).
struct SimplifyStats {
  std::size_t constantsFolded = 0; ///< Ops replaced by a folded Const COPY.
  std::size_t identitiesApplied = 0; ///< Ops replaced via an algebraic
                                      ///< identity/annihilator/no-op rule.
  std::size_t opsRemoved = 0; ///< Dead-Temp-defining ops dropped.
  std::size_t iterations = 0; ///< Fixed-point iterations actually run.

  [[nodiscard]] bool changed() const {
    return constantsFolded || identitiesApplied || opsRemoved;
  }
};

/// Peephole-simplifies `block` and returns a new Block: forward constant
/// folding/propagation, algebraic identity/annihilator elimination, no-op
/// width-conversion elimination, and dead-Temp elimination. Runs to a fixed
/// point (bounded by maxIterations) since one rewrite can expose another.
///
/// Block-local only: does not cross into predecessor/successor blocks (same
/// boundary BackwardSlicer already stops at), does not canonicalize memory
/// addresses, and never removes a Reg-defining op even if apparently unused
/// within the block (a Reg may be live-out across the block boundary; only
/// Temps are provably block-local).
[[nodiscard]] Block simplifyBlock(const Block &block, int maxIterations = 8);

/// Same as above, but also reports what changed via `stats`.
[[nodiscard]] Block simplifyBlock(const Block &block, SimplifyStats &stats,
                                  int maxIterations = 8);

} // namespace dragon::ignil
