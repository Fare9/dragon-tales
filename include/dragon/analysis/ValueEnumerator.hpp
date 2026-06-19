#pragma once

#include <dragon/analysis/SliceExpr.hpp>
#include <dragon/analysis/SliceExprZ3.hpp>

#include <cstdint>
#include <functional>
#include <vector>
#include <z3++.h>

namespace dragon::slice {

/// Used to configure the maximum number of solutions
struct EnumeratorConfig {
  std::size_t maxValues = 256; ///< Abort after this many solutions
  bool sorted = true; ///< Sort results before returning
};

/// Enumerate all concrete 64-bit values that `expr` can take given
/// the extra `constraints` (already expressed as Z3 assertions)
///
/// Each call to Z3 produces one model, then adds a blocking clause
/// (expr != found_value) before asking for the next. The loop ends
/// when the solver returns UNSAT or maxValues is reached.
///
/// Returns an empty vector when the expression is unsatisfiable under
/// the given constraints, or when the expression is fully concrete
/// (Const node) -> in the latter case the single constant value
/// is returned directly without calling Z3.
[[nodiscard]] std::vector<std::uint64_t>
enumeratedValues(const Expr &expr, z3::context &ctx,
                 const std::vector<z3::expr> &constraints = {},
                 const EnumeratorConfig &cfg = {});

/// Convenience: solve for a single concrete value.
/// Returns nullopt if unsatisfiable or if the expression is not
/// uniquely determined (i.e. more than one model exists).
[[nodiscard]] std::optional<std::uint64_t>
solveUnique(const Expr &expr, z3::context &ctx,
            const std::vector<z3::expr> &constraints = {});

} // namespace dragon::slice
