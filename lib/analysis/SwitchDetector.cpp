#include <dragon/analysis/BackwardSlicer.hpp>
#include <dragon/analysis/SliceExprMatch.hpp>
#include <dragon/analysis/SwitchDetector.hpp>
#include <dragon/disassembler/BasicBlock.hpp>
#include <dragon/disassembler/Graph.hpp>

#include <cstring>

namespace dragon::slice {

using namespace match;

// =============================================================================
// Internal helpers
// =============================================================================

// Compare two IGNIL registers by identity (offset + width, ignoring name).
static bool sameReg(const ignil::Reg &a, const ignil::Reg &b) {
  return a.offset == b.offset && a.width == b.width;
}

// Try to extract an upper case count from a sliced CJMP condition expression.
//
// The condition is the expression that, when non-zero, causes the branch to
// jump to the out-of-bounds (default) target.  Common patterns:
//
//   UGE(idx, N)                       -- JAE:  idx >= N, numCases = N
//   UGT(idx, N)                       -- JA:   idx > N,  numCases = N+1
//   AND(NOT(ULT(idx,N)), NOT(EQ(idx,N))) -- JA expanded by the lifter
//
// Returns 0 when no matching pattern is found.
static std::uint32_t extractBound(const Expr &cond,
                                  const ignil::Reg &indexReg) {
  ignil::Reg reg;
  std::uint64_t N = 0;

  // UGE(idx, N) -- jump if idx >= N (JAE / JNBE)
  if (match::match(cond,
                   m_BinOp(ignil::Opcode::UGE, m_SymReg(reg), m_Const(N))) &&
      sameReg(reg, indexReg))
    return static_cast<std::uint32_t>(N);

  // UGT(idx, N) -- jump if idx > N (JA / JNBE with N = max_case)
  if (match::match(cond,
                   m_BinOp(ignil::Opcode::UGT, m_SymReg(reg), m_Const(N))) &&
      sameReg(reg, indexReg))
    return static_cast<std::uint32_t>(N) + 1;

  // NOT(ULT(idx, N)) == UGE(idx, N): one branch of the JA AND-expansion
  if (match::match(cond, m_Not(m_BinOp(ignil::Opcode::ULT, m_SymReg(reg),
                                       m_Const(N)))) &&
      sameReg(reg, indexReg))
    return static_cast<std::uint32_t>(N);

  // AND(cond_a, cond_b): JA expands to AND(NOT(CF), NOT(ZF)); recurse into
  // each child and take the first successful extraction.
  if (const auto *b = std::get_if<BinOp>(&cond.data)) {
    if (b->op == ignil::Opcode::AND && b->left && b->right) {
      if (auto r = extractBound(*b->left, indexReg))
        return r;
      if (auto r = extractBound(*b->right, indexReg))
        return r;
    }
  }

  return 0;
}

// Search the graph for the unique predecessor block of blockAddr whose IGNIL
// representation ends with a CJMP.  Returns nullptr when no such predecessor
// exists or when there are multiple candidates (ambiguous / join node).
static const ignil::Block *findCJMPPredecessor(std::uint64_t blockAddr,
                                               const Graph &graph) {
  const ignil::Block *found = nullptr;

  for (const BasicBlock &b: graph.blocks()) {
    if (b.startAddress() == blockAddr)
      continue;
    if (!b.successors().count(blockAddr))
      continue;

    const ignil::Block *ib = graph.getIGNILBlock(b.startAddress());
    if (!ib || ib->empty())
      continue;
    if (ib->ops().back().opcode != ignil::Opcode::CJMP)
      continue;

    // More than one CJMP predecessor -- give up, not a simple switch guard.
    if (found)
      return nullptr;
    found = ib;
  }

  return found;
}

// =============================================================================
// Public API
// =============================================================================

std::optional<SwitchCandidate> detectPossibleSwitch(const Expr &jumpTarget,
                                                    std::uint64_t blockAddr,
                                                    const Graph &graph) {
  std::uint64_t tableBase = 0, scale = 0;
  ignil::Reg indexReg;

  // Phase 1: structural match on the slice expression.
  // Recognize: LOAD( ADD(Const{base}, MUL(SymReg{idx}, Const{scale})) )
  // m_AddAnyOrder / m_MulAnyOrder handle either operand ordering.
  // The matchers will retrieve the values in the code, as well as the register.
  if (!match::match(jumpTarget,
                    m_Load(m_AddAnyOrder(m_Const(tableBase),
                                         m_MulAnyOrder(m_SymReg(indexReg),
                                                       m_Const(scale))))))
    return std::nullopt;

  // Only 4-byte and 8-byte pointer tables are supported.
  if (scale != 4 && scale != 8)
    return std::nullopt;

  // Structure with possible candidates for the switch statement
  SwitchCandidate sw{tableBase, static_cast<std::uint32_t>(scale),
                     0, // numCases unknown until phase 2
                     indexReg};

  // Phase 2: extract case count from a predecessor bounds-check CJMP.
  if (const ignil::Block *predBlock = findCJMPPredecessor(blockAddr, graph)) {
    const ignil::Op &cjmp = predBlock->ops().back();

    // srcs[0] is the condition Value -- slice it backwards to get the
    // comparison expression.
    BackwardSlicer slicer{3};
    Expr condExpr =
            slicer.slice(cjmp.srcs[0], predBlock->startAddress(), graph);

    sw.numCases = extractBound(condExpr, indexReg);
  }

  return sw;
}

std::vector<std::uint64_t> readTableTargets(const SwitchCandidate &sw,
                                            std::span<const std::uint8_t> bytes,
                                            std::uint64_t baseAddr) {
  if (sw.numCases == 0)
    return {};
  if (sw.tableBase < baseAddr)
    return {};

  const std::size_t tableOffset =
          sw.tableBase - baseAddr;
  const std::size_t tableBytes = sw.numCases * sw.entrySize;

  if (tableOffset >= bytes.size() || tableBytes > bytes.size() - tableOffset)
    return {};

  std::vector<std::uint64_t> targets;
  targets.reserve(sw.numCases);

  for (std::uint32_t i = 0; i < sw.numCases; ++i) {
    const std::size_t offset = tableOffset + i * sw.entrySize;
    std::uint64_t target = 0;
    std::memcpy(&target, bytes.data() + offset, sw.entrySize);
    targets.push_back(target);
  }

  return targets;
}

} // namespace dragon::slice
