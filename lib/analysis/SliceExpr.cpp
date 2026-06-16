#include <dragon/analysis/SliceExpr.hpp>

#include "utils/Utils.hpp"

#include <format>

namespace dragon::slice {

std::string toString(const Expr &e) {
  return e.visit(utils::overloaded{
          [](const Const &c) {
            return std::format("0x{:x}:i{}", c.value, c.width);
          },
          [](const SymReg &r) {
            if (!r.reg.name.empty())
              return std::format("{}:i{}", r.reg.name, r.reg.width);
            return std::format("reg[{}]:i{}", r.reg.offset, r.reg.width);
          },
          [](const BinOp &b) {
            return std::format("{}({}, {})", ignil::opcodeName(b.op),
                               toString(*b.left), toString(*b.right));
          },
          [](const UnaryOp &u) {
            return std::format("{}({})", ignil::opcodeName(u.op),
                               toString(*u.operand));
          },
          [](const Load &l) {
            return std::format("MEM[{}]:i{}", toString(*l.addr), l.width);
          },
          [](const Undef &) -> std::string { return "undef"; },
  });
}

} // namespace dragon::slice
