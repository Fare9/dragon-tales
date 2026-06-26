#pragma once

#include <dragon/analysis/SliceExpr.hpp>
#include <dragon/lifters/ignil/IGNIL.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace dragon {
class Graph;
} // namespace dragon

namespace dragon::slice {

/// Everything extracted from a detected jump-table pattern.
struct SwitchCandidate {
    /// Base address of the jump table in the binary image.
    std::uint64_t tableBase;

    /// Bytes per table entry: 4 (32-bit pointers) or 8 (64-bit pointers).
    std::uint32_t entrySize;

    /// Number of table entries (cases).  0 means the bounds check was not
    /// found; readTableTargets will return an empty vector in that case.
    std::uint32_t numCases;

    /// Register used as the table index, as identified in the slice expression.
    ignil::Reg indexReg;
};

/// Inspect a backward-slice result and determine whether it is a jump-table
/// indirect branch.
///
/// Recognition is two-phase:
///   1. Pattern-match the slice expression for
///      LOAD( ADD(Const{base}, MUL(SymReg{idx}, Const{scale})) )
///      in any operand order.  entrySize must be 4 or 8.
///   2. Walk the predecessor blocks looking for a single CJMP whose sliced
///      condition can be matched against a comparison of indexReg with a
///      constant, giving the case count.
///
/// Returns nullopt when the expression does not match the pattern.
/// Returns a SwitchCandidate with numCases == 0 when the pattern is found
/// but bounds could not be extracted from a predecessor guard.
std::optional<SwitchCandidate> detectPossibleSwitch(const Expr &jumpTarget,
                                                    std::uint64_t blockAddr,
                                                    const Graph &graph);

/// Read numCases pointer-sized entries from the jump table encoded in bytes.
///
/// Entries are read as little-endian integers (suitable for x86/x86-64).
/// Returns an empty vector when numCases == 0 or the table extends outside
/// the provided byte range.
std::vector<std::uint64_t> readTableTargets(
        const SwitchCandidate &sw,
        std::span<const std::uint8_t> bytes,
        std::uint64_t baseAddr);

} // namespace dragon::slice
