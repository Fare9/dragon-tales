#include <dragon/lifters/ignil/IGNILSimplify.hpp>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace dragon::ignil {

namespace {

// ---------------------------------------------------------------------------
// Width-aware integer helpers
// ---------------------------------------------------------------------------

std::uint64_t maskTo(std::uint64_t v, Width w) {
  if (w >= 64)
    return v;
  return v & ((std::uint64_t{1} << w) - 1);
}

std::int64_t signExtend(std::uint64_t v, Width w) {
  if (w >= 64)
    return static_cast<std::int64_t>(v);
  std::uint64_t masked = maskTo(v, w);
  std::uint64_t signBit = std::uint64_t{1} << (w - 1);
  if (masked & signBit)
    masked |= ~((std::uint64_t{1} << w) - 1);
  return static_cast<std::int64_t>(masked);
}

bool isAllOnes(const Const &c) {
  return maskTo(c.value, c.width) == maskTo(~std::uint64_t{0}, c.width);
}

bool isZero(const Const &c) { return maskTo(c.value, c.width) == 0; }

// ---------------------------------------------------------------------------
// Constant folding
// ---------------------------------------------------------------------------

bool isBinaryFoldable(Opcode op) {
  switch (op) {
    case Opcode::ADD:
    case Opcode::SUB:
    case Opcode::MUL:
    case Opcode::AND:
    case Opcode::OR:
    case Opcode::XOR:
    case Opcode::SHL:
    case Opcode::SHR:
    case Opcode::SAR:
    case Opcode::SDIV:
    case Opcode::UDIV:
    case Opcode::SMOD:
    case Opcode::UMOD:
    case Opcode::EQ:
    case Opcode::NEQ:
    case Opcode::ULT:
    case Opcode::ULE:
    case Opcode::UGT:
    case Opcode::UGE:
    case Opcode::SLT:
    case Opcode::SLE:
    case Opcode::SGT:
    case Opcode::SGE:
      return true;
    default:
      return false;
  }
}

bool isUnaryFoldable(Opcode op) {
  switch (op) {
    case Opcode::NEG:
    case Opcode::NOT:
    case Opcode::ZEXT:
    case Opcode::SEXT:
    case Opcode::TRUNC:
      return true;
    default:
      return false;
  }
}

// dstWidth == 0 means "no dst / use operand width" (never hit for the
// opcodes handled here in well-formed IGNIL - ADD/SUB/... always have a
// dst - but treated defensively rather than assumed).
std::optional<Const> foldBinary(Opcode op, const Const &l, const Const &r,
                                Width dstWidth) {
  // Comparisons always produce a 1-bit result, independent of operand width.
  {
    std::uint64_t lv = maskTo(l.value, l.width);
    std::uint64_t rv = maskTo(r.value, r.width);
    std::int64_t ls = signExtend(l.value, l.width);
    std::int64_t rs = signExtend(r.value, r.width);
    switch (op) {
      case Opcode::EQ: return Const{lv == rv ? 1u : 0u, 1};
      case Opcode::NEQ: return Const{lv != rv ? 1u : 0u, 1};
      case Opcode::ULT: return Const{lv < rv ? 1u : 0u, 1};
      case Opcode::ULE: return Const{lv <= rv ? 1u : 0u, 1};
      case Opcode::UGT: return Const{lv > rv ? 1u : 0u, 1};
      case Opcode::UGE: return Const{lv >= rv ? 1u : 0u, 1};
      case Opcode::SLT: return Const{ls < rs ? 1u : 0u, 1};
      case Opcode::SLE: return Const{ls <= rs ? 1u : 0u, 1};
      case Opcode::SGT: return Const{ls > rs ? 1u : 0u, 1};
      case Opcode::SGE: return Const{ls >= rs ? 1u : 0u, 1};
      default: break;
    }
  }

  Width w = dstWidth ? dstWidth : std::max(l.width, r.width);
  std::uint64_t lv = maskTo(l.value, l.width);
  std::uint64_t rv = maskTo(r.value, r.width);
  std::int64_t ls = signExtend(l.value, l.width);
  std::int64_t rs = signExtend(r.value, r.width);
  // Raw (unmasked-to-w) shift amount - a shift count stored in a narrower
  // Const than the shifted value is still a valid amount to compare to w.
  std::uint64_t shiftAmount = maskTo(r.value, r.width);

  switch (op) {
    case Opcode::ADD: return Const{maskTo(lv + rv, w), w};
    case Opcode::SUB: return Const{maskTo(lv - rv, w), w};
    case Opcode::MUL: return Const{maskTo(lv * rv, w), w};
    case Opcode::AND: return Const{maskTo(lv & rv, w), w};
    case Opcode::OR: return Const{maskTo(lv | rv, w), w};
    case Opcode::XOR: return Const{maskTo(lv ^ rv, w), w};
    case Opcode::SHL:
      return Const{shiftAmount >= w ? 0 : maskTo(lv << shiftAmount, w), w};
    case Opcode::SHR:
      return Const{shiftAmount >= w ? 0 : maskTo(lv >> shiftAmount, w), w};
    case Opcode::SAR: {
      if (shiftAmount >= w)
        return Const{ls < 0 ? maskTo(~std::uint64_t{0}, w) : 0, w};
      return Const{maskTo(static_cast<std::uint64_t>(ls >> shiftAmount), w),
                   w};
    }
    // Division/modulo by zero: leave unfolded rather than guessing trap
    // semantics IGNIL doesn't model.
    case Opcode::UDIV:
      return rv == 0 ? std::nullopt
                      : std::optional<Const>{Const{maskTo(lv / rv, w), w}};
    case Opcode::SDIV:
      return rs == 0 ? std::nullopt
                      : std::optional<Const>{Const{
                                maskTo(static_cast<std::uint64_t>(ls / rs), w),
                                w}};
    case Opcode::UMOD:
      return rv == 0 ? std::nullopt
                      : std::optional<Const>{Const{maskTo(lv % rv, w), w}};
    case Opcode::SMOD:
      return rs == 0 ? std::nullopt
                      : std::optional<Const>{Const{
                                maskTo(static_cast<std::uint64_t>(ls % rs), w),
                                w}};
    default:
      return std::nullopt;
  }
}

std::optional<Const> foldUnary(Opcode op, const Const &v, Width dstWidth) {
  std::uint64_t vv = maskTo(v.value, v.width);
  switch (op) {
    case Opcode::NEG:
      return Const{maskTo(static_cast<std::uint64_t>(-static_cast<std::int64_t>(vv)),
                          v.width),
                   v.width};
    case Opcode::NOT:
      return Const{maskTo(~vv, v.width), v.width};
    case Opcode::ZEXT: {
      Width dw = dstWidth ? dstWidth : v.width;
      return Const{maskTo(vv, dw), dw};
    }
    case Opcode::SEXT: {
      Width dw = dstWidth ? dstWidth : v.width;
      return Const{maskTo(static_cast<std::uint64_t>(signExtend(v.value, v.width)),
                          dw),
                   dw};
    }
    case Opcode::TRUNC: {
      Width dw = dstWidth ? dstWidth : v.width;
      return Const{maskTo(vv, dw), dw};
    }
    default:
      return std::nullopt;
  }
}

bool allConst(const std::vector<Value> &srcs) {
  return std::all_of(srcs.begin(), srcs.end(), [](const Value &v) {
    return std::holds_alternative<Const>(v);
  });
}

Op copyOfConst(const std::optional<Value> &dst, Const c, std::uint64_t addr) {
  Op o;
  o.opcode = Opcode::COPY;
  o.dst = dst;
  o.srcs = {Value{c}};
  o.address = addr;
  return o;
}

// ---------------------------------------------------------------------------
// Algebraic identity / annihilator elimination - exactly one of {lhs, rhs}
// is a literal Const, the other is left symbolic. Requires matching operand
// widths (same-width ALU semantics - IGNIL never mixes widths on a single
// ADD/AND/OR/... without an explicit ZEXT/SEXT/TRUNC first).
// ---------------------------------------------------------------------------

std::optional<Op> tryIdentity(Opcode op, const Value &lhs, const Value &rhs,
                              const std::optional<Value> &dst,
                              std::uint64_t addr) {
  const auto *cl = std::get_if<Const>(&lhs);
  const auto *cr = std::get_if<Const>(&rhs);
  if (static_cast<bool>(cl) == static_cast<bool>(cr))
    return std::nullopt; // need exactly one const side

  if (widthOf(lhs) != widthOf(rhs))
    return std::nullopt;
  Width w = widthOf(lhs);

  auto asCopy = [&](Value v) {
    Op o;
    o.opcode = Opcode::COPY;
    o.dst = dst;
    o.srcs = {std::move(v)};
    o.address = addr;
    return o;
  };
  auto asCopyConst = [&](std::uint64_t val) {
    return asCopy(Value{Const{maskTo(val, w), w}});
  };

  if (cl) {
    // Const <op> rhs
    const Const &c = *cl;
    switch (op) {
      case Opcode::ADD:
        if (isZero(c)) return asCopy(rhs);
        break;
      case Opcode::AND:
        if (isAllOnes(c)) return asCopy(rhs);
        if (isZero(c)) return asCopyConst(0);
        break;
      case Opcode::OR:
        if (isZero(c)) return asCopy(rhs);
        if (isAllOnes(c)) return asCopyConst(~std::uint64_t{0});
        break;
      case Opcode::XOR:
        if (isZero(c)) return asCopy(rhs);
        break;
      case Opcode::MUL:
        if (c.value == 1) return asCopy(rhs);
        if (isZero(c)) return asCopyConst(0);
        break;
      default:
        break;
    }
  } else {
    // lhs <op> Const
    const Const &c = *cr;
    switch (op) {
      case Opcode::ADD:
      case Opcode::SUB:
        if (isZero(c)) return asCopy(lhs);
        break;
      case Opcode::AND:
        if (isAllOnes(c)) return asCopy(lhs);
        if (isZero(c)) return asCopyConst(0);
        break;
      case Opcode::OR:
        if (isZero(c)) return asCopy(lhs);
        if (isAllOnes(c)) return asCopyConst(~std::uint64_t{0});
        break;
      case Opcode::XOR:
        if (isZero(c)) return asCopy(lhs);
        break;
      case Opcode::MUL:
        if (c.value == 1) return asCopy(lhs);
        if (isZero(c)) return asCopyConst(0);
        break;
      case Opcode::SHL:
      case Opcode::SHR:
      case Opcode::SAR:
        if (isZero(c)) return asCopy(lhs);
        break;
      default:
        break;
    }
  }
  return std::nullopt;
}

bool hasSideEffect(Opcode op) {
  switch (op) {
    case Opcode::STORE:
    case Opcode::JMP:
    case Opcode::CJMP:
    case Opcode::CALL:
    case Opcode::RET:
      return true;
    default:
      // Includes LOAD: IGNIL has no volatile/side-effecting LOAD, so an
      // unused load result is as removable as any other pure Temp def.
      return false;
  }
}

// ---------------------------------------------------------------------------
// Forward pass: constant fold + propagate (Temps only - Reg is not
// guaranteed single-assignment within a block, so it is deliberately not
// tracked here) + algebraic identities + no-op width conversions.
// ---------------------------------------------------------------------------

Block passForward(const Block &in, SimplifyStats &stats) {
  Block out{in.startAddress()};
  // Records `t = COPY(v)` so later uses of `t` substitute `v` directly -
  // general copy propagation, not just constant propagation: `v` may be a
  // Const, a Reg, or another (already-resolved) Temp. Temp-only, since Temps
  // are the only IGNIL value guaranteed single-assignment within a block
  // (same invariant BackwardSlicer::sliceValue relies on for Temps vs Reg).
  std::unordered_map<std::uint32_t, Value> copyOf;
  // Records `t = NEG(x)` / `t = NOT(x)` so a later NEG(t)/NOT(t) collapses to
  // COPY(x). Same Temp-only reasoning as copyOf.
  std::unordered_map<std::uint32_t, std::pair<Opcode, Value>> unaryDefOf;

  auto substitute = [&](const Value &v) -> Value {
    if (const auto *t = std::get_if<Temp>(&v)) {
      auto it = copyOf.find(t->id);
      if (it != copyOf.end())
        return it->second;
    }
    return v;
  };

  for (const Op &op: in) {
    Op newOp = op;
    for (auto &s: newOp.srcs)
      s = substitute(s);

    bool handled = false;
    Width dstWidth = newOp.dst ? widthOf(*newOp.dst) : Width{0};

    if (allConst(newOp.srcs)) {
      if (newOp.srcs.size() == 1 && isUnaryFoldable(newOp.opcode)) {
        if (auto folded = foldUnary(newOp.opcode, std::get<Const>(newOp.srcs[0]),
                                    dstWidth)) {
          newOp = copyOfConst(newOp.dst, *folded, newOp.address);
          handled = true;
          ++stats.constantsFolded;
        }
      } else if (newOp.srcs.size() == 2 && isBinaryFoldable(newOp.opcode)) {
        if (auto folded = foldBinary(newOp.opcode, std::get<Const>(newOp.srcs[0]),
                                     std::get<Const>(newOp.srcs[1]), dstWidth)) {
          newOp = copyOfConst(newOp.dst, *folded, newOp.address);
          handled = true;
          ++stats.constantsFolded;
        }
      }
    }

    if (!handled && newOp.srcs.size() == 2) {
      if (auto rw = tryIdentity(newOp.opcode, newOp.srcs[0], newOp.srcs[1],
                                newOp.dst, newOp.address)) {
        newOp = std::move(*rw);
        handled = true;
        ++stats.identitiesApplied;
      }
    }

    if (!handled && newOp.srcs.size() == 1 &&
        (newOp.opcode == Opcode::NEG || newOp.opcode == Opcode::NOT)) {
      if (const auto *t = std::get_if<Temp>(&newOp.srcs[0])) {
        auto it = unaryDefOf.find(t->id);
        if (it != unaryDefOf.end() && it->second.first == newOp.opcode &&
            newOp.dst && widthOf(it->second.second) == widthOf(*newOp.dst)) {
          newOp.opcode = Opcode::COPY;
          newOp.srcs = {it->second.second};
          handled = true;
          ++stats.identitiesApplied;
        }
      }
    }

    if (!handled && newOp.srcs.size() == 1 &&
        (newOp.opcode == Opcode::ZEXT || newOp.opcode == Opcode::SEXT ||
         newOp.opcode == Opcode::TRUNC) &&
        newOp.dst && widthOf(newOp.srcs[0]) == widthOf(*newOp.dst)) {
      newOp.opcode = Opcode::COPY;
      handled = true;
      ++stats.identitiesApplied;
    }

    if (newOp.dst) {
      if (const auto *dt = std::get_if<Temp>(&*newOp.dst)) {
        copyOf.erase(dt->id);
        unaryDefOf.erase(dt->id);
        if (newOp.opcode == Opcode::COPY && newOp.srcs.size() == 1) {
          copyOf.emplace(dt->id, newOp.srcs[0]);
        } else if ((newOp.opcode == Opcode::NEG || newOp.opcode == Opcode::NOT) &&
                   newOp.srcs.size() == 1) {
          // Recorded regardless of the operand's kind (Reg or Temp) - it's
          // dt->id (a Temp, guaranteed single-assignment) that makes this
          // record safe to trust later, not the operand. The *lookup* side
          // below is what restricts collapsing to Temp operands only.
          unaryDefOf.emplace(dt->id, std::pair{newOp.opcode, newOp.srcs[0]});
        }
      }
    }

    out.append(std::move(newOp));
  }
  return out;
}

// ---------------------------------------------------------------------------
// Backward pass: drop Temp-defining ops whose result is never read later in
// the block. Reg-defining ops are always kept - a Reg may be live-out across
// the block boundary, which this block-local pass cannot see.
// ---------------------------------------------------------------------------

Block passBackwardDCE(const Block &in, SimplifyStats &stats) {
  const std::vector<Op> &ops = in.ops();
  std::unordered_set<std::uint32_t> usedTemps;
  std::vector<bool> keep(ops.size(), true);

  for (std::size_t i = ops.size(); i-- > 0;) {
    const Op &op = ops[i];
    bool dead = false;
    if (op.dst && !hasSideEffect(op.opcode)) {
      if (const auto *t = std::get_if<Temp>(&*op.dst))
        dead = !usedTemps.count(t->id);
    }
    if (dead) {
      keep[i] = false;
      ++stats.opsRemoved;
      continue; // dropped - its own srcs don't count as uses
    }
    for (const auto &s: op.srcs) {
      if (const auto *t = std::get_if<Temp>(&s))
        usedTemps.insert(t->id);
    }
  }

  Block out{in.startAddress()};
  for (std::size_t i = 0; i < ops.size(); ++i)
    if (keep[i])
      out.append(ops[i]);
  return out;
}

} // namespace

Block simplifyBlock(const Block &block, SimplifyStats &stats,
                    int maxIterations) {
  stats = SimplifyStats{};
  Block current = block;
  for (int iter = 0; iter < maxIterations; ++iter) {
    SimplifyStats iterStats;
    Block afterForward = passForward(current, iterStats);
    Block afterDCE = passBackwardDCE(afterForward, iterStats);

    stats.constantsFolded += iterStats.constantsFolded;
    stats.identitiesApplied += iterStats.identitiesApplied;
    stats.opsRemoved += iterStats.opsRemoved;
    stats.iterations = iter + 1;

    current = std::move(afterDCE);
    if (!iterStats.changed())
      break;
  }
  return current;
}

Block simplifyBlock(const Block &block, int maxIterations) {
  SimplifyStats stats;
  return simplifyBlock(block, stats, maxIterations);
}

} // namespace dragon::ignil
