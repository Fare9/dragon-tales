#include <dragon/analysis/BackwardSlicer.hpp>
#include <dragon/disassembler/Graph.hpp>

#include "utils/Utils.hpp"

#include <unordered_map>
#include <vector>

namespace dragon {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

struct SuccessorInfo {
  std::vector<std::uint64_t> known;
  bool hasIndirect = false;
};

// Collect all direct successors of a block by inspecting its terminal Op.
// Returns up to two target addresses (JMP → one, CJMP → two).
SuccessorInfo successorsOf(const ignil::Block &block) {
  if (block.empty())
    return {};
  // Retrieve the last instruction, it must be a jump, ret,
  // etc.
  const ignil::Op &last = block.ops().back();
  SuccessorInfo info;

  auto pushValue = [&](const ignil::Value &v) {
    // If the value from the JMP instruction is a constant
    // we don't need any calculation
    if (const auto *c = std::get_if<ignil::Const>(&v))
      info.known.push_back(c->value);
    else
      info.hasIndirect = true;
  };
  if (last.opcode == ignil::Opcode::JMP && !last.srcs.empty()) {
    pushValue(last.srcs[0]);
  } else if (last.opcode == ignil::Opcode::CJMP && last.srcs.size() >= 3) {
    pushValue(last.srcs[1]); // taken
    pushValue(last.srcs[2]); // fallthrough
  }
  return info;
}

} // namespace

// ---------------------------------------------------------------------------
// BackwardSlicer
// ---------------------------------------------------------------------------

BackwardSlicer::BackwardSlicer(int maxBlocks) : maxBlocks_(maxBlocks) {}

// Build a map from block_addr → unique_predecessor_addr.
// Blocks with 0 or ≥2 predecessors are absent (entry / join nodes).
BackwardSlicer::PredMap
BackwardSlicer::buildPredecessorMap(const Graph &graph) const {
  // predecessor count: addr → list of predecessor addresses
  std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> incoming;
  for (std::uint64_t addr: graph.blocks_q.valid()) {
    // Initialize the incoming maps with all the blocks
    incoming.emplace(addr, std::vector<std::uint64_t>{});
  }
  for (std::uint64_t addr: graph.blocks_q.valid()) {
    // retrieve the ignil block from address
    const ignil::Block *blk = graph.getIGNILBlock(addr);
    if (!blk)
      continue;
    // look for the list of successors using the IGNIL instructions
    // this will make the search, architecture-agnostic.
    for (std::uint64_t succ: successorsOf(*blk).known) {
      if (auto it = incoming.find(succ); it != incoming.end())
        it->second.push_back(addr);
    }
  }
  // Keep only single-predecessor entries
  PredMap result;
  for (auto &[addr, preds]: incoming) {
    if (preds.size() == 1)
      result[addr] = preds[0];
  }
  return result;
}

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

slice::Expr BackwardSlicer::slice(const ignil::Value &root,
                                  std::uint64_t blockAddr,
                                  const Graph &graph) const {
  const PredMap preds = buildPredecessorMap(graph);
  const ignil::Block *blk = graph.getIGNILBlock(blockAddr);
  if (!blk)
    return slice::Expr{slice::Undef{}};
  int startIdx = static_cast<int>(blk->ops().size()); // scan all ops
  return sliceValue(root, *blk, startIdx, graph, preds, maxBlocks_);
}

// ---------------------------------------------------------------------------
// sliceValue — resolve a Value backwards through ops
// ---------------------------------------------------------------------------

slice::Expr BackwardSlicer::sliceValue(const ignil::Value &v,
                                       const ignil::Block &block, int beforeIdx,
                                       const Graph &graph, const PredMap &preds,
                                       int depth) const {
  return std::visit(
          utils::overloaded{
                  // Constants are trivially resolved.
                  [](const ignil::Const &c) -> slice::Expr {
                    return slice::Expr{slice::Const{c.value, c.width}};
                  },

                  // Temps are single-assignment within a block; scan backwards
                  // for
                  // the defining COPY/binary/unary/LOAD op.
                  [&](const ignil::Temp &t) -> slice::Expr {
                    const auto &ops = block.ops();
                    for (int i = beforeIdx - 1; i >= 0; --i) {
                      // retrieve operation
                      const ignil::Op &op = ops[i];
                      if (!op.dst)
                        continue;
                      // check if destination is a temporal, and it is the same
                      // temporal since we use SSA, there is where the temporal
                      // has been defined.
                      if (const auto *dt = std::get_if<ignil::Temp>(&*op.dst)) {
                        if (dt->id == t.id)
                          return buildFromOp(op, block, i, graph, preds, depth);
                      }
                    }
                    // Not defined in this block — temps never cross blocks,
                    // treat as undef.
                    return slice::Expr{slice::Undef{}};
                  },

                  // Registers: scan backwards within the block for the last
                  // write.
                  // If not found, and we still have depth budget, cross into
                  // the unique
                  // predecessor block.
                  [&](const ignil::Reg &r) -> slice::Expr {
                    const auto &ops = block.ops();
                    for (int i = beforeIdx - 1; i >= 0; --i) {
                      const ignil::Op &op = ops[i];
                      if (!op.dst)
                        continue;
                      // Same as with temporal, look the destination is the
                      // register with same offset and same width, maybe in
                      // the future we can check with different width too
                      if (const auto *dr = std::get_if<ignil::Reg>(&*op.dst)) {
                        if (dr->offset == r.offset && dr->width == r.width)
                          return buildFromOp(op, block, i, graph, preds, depth);
                      }
                    }
                    // Not defined here — try predecessor block.
                    if (depth <= 0)
                      return slice::Expr{slice::SymReg{r}};
                    auto predIt = preds.find(block.startAddress());
                    if (predIt == preds.end())
                      return slice::Expr{slice::SymReg{r}}; // join or entry
                    const ignil::Block *predBlk =
                            graph.getIGNILBlock(predIt->second);
                    if (!predBlk)
                      return slice::Expr{slice::SymReg{r}};
                    int endIdx = static_cast<int>(predBlk->ops().size());
                    return sliceValue(v, *predBlk, endIdx, graph, preds,
                                      depth - 1);
                  },
          },
          v);
}

// ---------------------------------------------------------------------------
// buildFromOp — turn the defining Op into a SliceExpr
// ---------------------------------------------------------------------------

slice::Expr BackwardSlicer::buildFromOp(const ignil::Op &op,
                                        const ignil::Block &block, int opIdx,
                                        const Graph &graph,
                                        const PredMap &preds, int depth) const {
  using Opc = ignil::Opcode;

  auto src = [&](int i) -> slice::Expr {
    if (i >= static_cast<int>(op.srcs.size()))
      return slice::Expr{slice::Undef{}};
    return sliceValue(op.srcs[i], block, opIdx, graph, preds, depth);
  };

  switch (op.opcode) {
    // Data movement — just forward the single source.
    case Opc::COPY:
      return src(0);

    // Memory load — fold address expression.
    case Opc::LOAD: {
      auto addrExpr = src(0);
      ignil::Width w = op.dst ? ignil::widthOf(*op.dst) : 64;
      return slice::Expr{slice::Load{
              std::make_shared<slice::Expr>(std::move(addrExpr)), w}};
    }

    // Binary arithmetic / bitwise / comparison ops.
    case Opc::ADD:
    case Opc::SUB:
    case Opc::MUL:
    case Opc::AND:
    case Opc::OR:
    case Opc::XOR:
    case Opc::SHL:
    case Opc::SHR:
    case Opc::SAR:
    case Opc::SDIV:
    case Opc::UDIV:
    case Opc::SMOD:
    case Opc::UMOD:
    case Opc::EQ:
    case Opc::NEQ:
    case Opc::ULT:
    case Opc::ULE:
    case Opc::UGT:
    case Opc::UGE:
    case Opc::SLT:
    case Opc::SLE:
    case Opc::SGT:
    case Opc::SGE: {
      auto left = src(0);
      auto right = src(1);
      return slice::Expr{slice::BinOp{
              op.opcode, std::make_shared<slice::Expr>(std::move(left)),
              std::make_shared<slice::Expr>(std::move(right))}};
    }

    // Unary ops.
    case Opc::NEG:
    case Opc::NOT:
    case Opc::ZEXT:
    case Opc::SEXT:
    case Opc::TRUNC: {
      auto operand = src(0);
      ignil::Width dstWidth = op.dst ? ignil::widthOf(*op.dst) : 0;
      return slice::Expr{slice::UnaryOp{
              op.opcode, std::make_shared<slice::Expr>(std::move(operand)),
              dstWidth}};
    }

    // UNDEF and control-flow ops produce no useful expression.
    default:
      return slice::Expr{slice::Undef{}};
  }
}

} // namespace dragon
