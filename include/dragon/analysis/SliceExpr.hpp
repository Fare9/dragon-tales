#pragma once

#include <dragon/lifters/ignil/IGNIL.hpp>

#include <memory>
#include <string>
#include <variant>

namespace dragon::slice {

/// Forward declaration needed by BinOp, UnaryOp, and Load,
/// which hold shared_ptr<Expr> children.
struct Expr;

/// A constant integer value.
struct Const {
  std::uint64_t value;
  ignil::Width width;
};

/// A register whose definition could not be traced further.
/// Treated as a symbolic / unknown input by the Z3 lowerer.
struct SymReg {
  ignil::Reg reg; ///< The register will depend on the architecture
                  ///< but for ignil the register is architecture agnostic.
};

/// Result of a binary operation (same opcodes as ignil::Opcode).
struct BinOp {
  ignil::Opcode op; ///< Binary operation
  std::shared_ptr<Expr> left; ///< Left expression
  std::shared_ptr<Expr> right; ///< Right expression
};

/// Result of a unary operation (NEG, NOT, ZEXT, SEXT, TRUNC).
struct UnaryOp {
  ignil::Opcode op; ///< Unary operation
  std::shared_ptr<Expr> operand; ///< Operand for unary operation
  ignil::Width resultWidth{
          0}; ///< Target width for ZEXT/SEXT/TRUNC; 0 = same as operand
};

/// Value loaded from memory at a computed address.
struct Load {
  std::shared_ptr<Expr>
          addr; ///< Keep for the address an expression that can be solved
  ignil::Width width; ///< width for that address in bits
};

/// Slicer could not determine the value (UNDEF source, depth exceeded,
/// or a join node was reached).
struct Undef {};

/// Neutral symbolic expression produced by backward slicing.
/// Use the Z3 lowerer to turn it into a solver formula.
struct Expr {
  using Var = std::variant<Const, SymReg, BinOp, UnaryOp, Load, Undef>;
  Var data;

  template<typename T>
  explicit Expr(T &&v) : data(std::forward<T>(v)) {}

  template<typename Visitor>
  decltype(auto) visit(Visitor &&vis) const {
    return std::visit(std::forward<Visitor>(vis), data);
  }
};

/// Human-readable rendering for debugging.
[[nodiscard]] std::string toString(const Expr &e);

} // namespace dragon::slice
