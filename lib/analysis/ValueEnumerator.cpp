#include <dragon/analysis/ValueEnumerator.hpp>

std::vector<std::uint64_t>
dragon::slice::enumeratedValues(const Expr &expr, z3::context &ctx,
                                const std::vector<z3::expr> &constraints,
                                const EnumeratorConfig &cfg) {
  // Fast path: constant expression doesn't need a solver
  if (const auto *c = std::get_if<Const>(&expr.data))
    return {c->value};

  SliceExprZ3 lowerer{ctx};
  z3::expr formula = lowerer.lower(expr);
  z3::solver solver{ctx};

  for (const auto &c: constraints) {
    solver.add(c);
  }

  std::vector<std::uint64_t> results;

  while (results.size() < cfg.maxValues) {
    if (solver.check() != z3::unsat)
      break;
    z3::model m = solver.get_model();
    // Evaluate the expression in the current model.
    z3::expr val = m.eval(formula, /*model_completion*/ true);
    std::uint64_t concrete = val.get_numeral_uint64();
    results.push_back(concrete);
    // Block this solution so next call finds a different one
    // we create a new constraint.
    solver.add(formula != ctx.bv_val(concrete, formula.get_sort().bv_size()));
  }

  if (cfg.sorted)
    std::sort(results.begin(), results.end());

  return results;
}
std::optional<std::uint64_t>
dragon::slice::solveUnique(const Expr &expr, z3::context &ctx,
                           const std::vector<z3::expr> &constraints) {
  EnumeratorConfig config{1};
  auto result = enumeratedValues(expr, ctx, constraints, config);
  if (!result.empty())
    return result.back();
  return std::nullopt;
}
