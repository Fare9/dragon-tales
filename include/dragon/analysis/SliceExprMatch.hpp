#pragma once

/// Composable structural pattern matching for SliceExpr trees.
///
/// Usage:
///   using namespace dragon::slice::match;
///
///   uint64_t base; ignil::Reg idx; uint64_t scale;
///   if (match(expr, m_Load(m_Add(m_Const(base),
///                               m_Mul(m_SymReg(idx), m_Const(scale))))))
///       // base, idx, scale are bound
///
/// Each matcher returns true when the expression matches and binds any
/// captured output variables as a side effect.  Captures are only written
/// on a full match -- a partial match that ultimately fails leaves the
/// capture variables untouched (the match short-circuits).
///
/// Note: BinOp matchers are NOT commutative by default.  Use m_AnyOrder
/// around a BinOp matcher to try both operand orderings.

#include <dragon/analysis/SliceExpr.hpp>
#include <cstdint>

namespace dragon::slice::match {

// =============================================================================
// Top-level entry point
// =============================================================================

/// Match expr against pattern.  Returns true if the pattern matches.
template<typename Matcher>
inline bool match(const Expr &expr, Matcher &&m) {
    return m(expr);
}

// =============================================================================
// Leaf matchers
// =============================================================================

/// Matches any expression unconditionally.
struct AnyMatcher {
    bool operator()(const Expr &) const { return true; }
};

inline AnyMatcher m_Any() { return {}; }

// -----------------------------------------------------------------------------

/// Matches a Const node.  Optionally captures value and/or width.
struct ConstMatcher {
    std::uint64_t  *value = nullptr;
    ignil::Width   *width = nullptr;

    bool operator()(const Expr &e) const {
        const auto *c = std::get_if<Const>(&e.data);
        if (!c) return false;
        if (value) *value = c->value;
        if (width) *width = c->width;
        return true;
    }
};

inline ConstMatcher m_Const() { return {}; }
inline ConstMatcher m_Const(std::uint64_t &v) { return {&v, nullptr}; }
inline ConstMatcher m_Const(std::uint64_t &v, ignil::Width &w) { return {&v, &w}; }

// -----------------------------------------------------------------------------

/// Matches a SymReg node.  Optionally captures the Reg descriptor.
struct SymRegMatcher {
    ignil::Reg *reg = nullptr;

    bool operator()(const Expr &e) const {
        const auto *r = std::get_if<SymReg>(&e.data);
        if (!r) return false;
        if (reg) *reg = r->reg;
        return true;
    }
};

inline SymRegMatcher m_SymReg() { return {}; }
inline SymRegMatcher m_SymReg(ignil::Reg &r) { return {&r}; }

// =============================================================================
// Structural matchers
// =============================================================================

/// Matches a Load node.  The address sub-expression is matched against
/// AddrMatcher.  Optionally captures the load width.
template<typename AddrMatcher>
struct LoadMatcher {
    AddrMatcher  addrMatcher;
    ignil::Width *width = nullptr;

    bool operator()(const Expr &e) const {
        const auto *l = std::get_if<Load>(&e.data);
        if (!l || !l->addr) return false;
        if (!addrMatcher(*l->addr)) return false;
        if (width) *width = l->width;
        return true;
    }
};

template<typename AM>
inline LoadMatcher<AM> m_Load(AM addrMatcher) {
    return {addrMatcher, nullptr};
}
template<typename AM>
inline LoadMatcher<AM> m_Load(AM addrMatcher, ignil::Width &w) {
    return {addrMatcher, &w};
}

// -----------------------------------------------------------------------------

/// Matches a BinOp node with a specific opcode.
/// Left and right sub-expressions are matched against LM and RM respectively.
template<typename LM, typename RM>
struct BinOpMatcher {
    ignil::Opcode opcode;
    LM left;
    RM right;

    bool operator()(const Expr &e) const {
        const auto *b = std::get_if<BinOp>(&e.data);
        if (!b || b->op != opcode) return false;
        if (!b->left || !b->right) return false;
        return left(*b->left) && right(*b->right);
    }
};

template<typename LM, typename RM>
inline BinOpMatcher<LM, RM> m_BinOp(ignil::Opcode op, LM lm, RM rm) {
    return {op, lm, rm};
}

template<typename LM, typename RM>
inline auto m_Add(LM lm, RM rm) {
    return m_BinOp(ignil::Opcode::ADD, lm, rm);
}
template<typename LM, typename RM>
inline auto m_Sub(LM lm, RM rm) {
    return m_BinOp(ignil::Opcode::SUB, lm, rm);
}
template<typename LM, typename RM>
inline auto m_Mul(LM lm, RM rm) {
    return m_BinOp(ignil::Opcode::MUL, lm, rm);
}
template<typename LM, typename RM>
inline auto m_And(LM lm, RM rm) {
    return m_BinOp(ignil::Opcode::AND, lm, rm);
}
template<typename LM, typename RM>
inline auto m_Or(LM lm, RM rm) {
    return m_BinOp(ignil::Opcode::OR, lm, rm);
}
template<typename LM, typename RM>
inline auto m_Shl(LM lm, RM rm) {
    return m_BinOp(ignil::Opcode::SHL, lm, rm);
}

// -----------------------------------------------------------------------------

/// Matches a UnaryOp node with a specific opcode.
/// Optionally captures the result width (relevant for ZEXT/SEXT/TRUNC).
template<typename OM>
struct UnaryOpMatcher {
    ignil::Opcode  opcode;
    OM             operandMatcher;
    ignil::Width  *resultWidth = nullptr;

    bool operator()(const Expr &e) const {
        const auto *u = std::get_if<UnaryOp>(&e.data);
        if (!u || u->op != opcode) return false;
        if (!u->operand) return false;
        if (!operandMatcher(*u->operand)) return false;
        if (resultWidth) *resultWidth = u->resultWidth;
        return true;
    }
};

template<typename OM>
inline UnaryOpMatcher<OM> m_UnaryOp(ignil::Opcode op, OM om) {
    return {op, om, nullptr};
}
template<typename OM>
inline UnaryOpMatcher<OM> m_UnaryOp(ignil::Opcode op, OM om, ignil::Width &w) {
    return {op, om, &w};
}

template<typename OM>
inline auto m_ZExt(OM om) { return m_UnaryOp(ignil::Opcode::ZEXT, om); }
template<typename OM>
inline auto m_SExt(OM om) { return m_UnaryOp(ignil::Opcode::SEXT, om); }
template<typename OM>
inline auto m_Trunc(OM om) { return m_UnaryOp(ignil::Opcode::TRUNC, om); }
template<typename OM>
inline auto m_Neg(OM om) { return m_UnaryOp(ignil::Opcode::NEG, om); }
template<typename OM>
inline auto m_Not(OM om) { return m_UnaryOp(ignil::Opcode::NOT, om); }

// =============================================================================
// Combinators
// =============================================================================

/// Tries inner matcher with operands in the original order; if that fails,
/// retries with the operands swapped.  Useful for commutative ops (ADD, MUL)
/// when the compiler may have placed the constant on either side.
///
/// Example:
///   m_AnyOrder(m_Add(m_Const(base), m_SymReg(idx)))
///   matches both ADD(base, idx) and ADD(idx, base).
template<typename LM, typename RM>
struct AnyOrderMatcher {
    ignil::Opcode opcode;
    LM left;
    RM right;

    bool operator()(const Expr &e) const {
        const auto *b = std::get_if<BinOp>(&e.data);
        if (!b || b->op != opcode) return false;
        if (!b->left || !b->right) return false;
        if (left(*b->left) && right(*b->right)) return true;
        // retry swapped
        return left(*b->right) && right(*b->left);
    }
};

/// Match a commutative BinOp trying both operand orderings.
template<typename LM, typename RM>
inline AnyOrderMatcher<LM, RM> m_AnyOrder(ignil::Opcode op, LM lm, RM rm) {
    return {op, lm, rm};
}

/// Commutative ADD shorthand.
template<typename LM, typename RM>
inline auto m_AddAnyOrder(LM lm, RM rm) {
    return m_AnyOrder(ignil::Opcode::ADD, lm, rm);
}

/// Commutative MUL shorthand.
template<typename LM, typename RM>
inline auto m_MulAnyOrder(LM lm, RM rm) {
    return m_AnyOrder(ignil::Opcode::MUL, lm, rm);
}

} // namespace dragon::slice::match
