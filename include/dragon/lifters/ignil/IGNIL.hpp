#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace dragon::ignil {

/// Width of a value or operation in bits.
/// Common widths: 1 (flag/boolean), 8, 16, 32, 64.
using Width = std::uint8_t;

// -----------------------------------------------------------------------
// Values - the operands and destinations of IGNIL operations
// -----------------------------------------------------------------------

/// A constant integer value of a given bit width.
/// Signedness is decided by the operation.
struct Const {
  std::uint64_t value;
  Width width;
};

/// An architectural register - these registers are modeled as a
/// location in the register address space. The lifter assigns
/// offsets; IGNIL itself is architecture-agnostic.
///
/// Sub-registers use the same base offset with different size:
///   RAX -> Reg{0, 64}
///   EAX -> Reg{0, 32}
///   AX  -> Reg{0, 16}
///   AL  -> Reg{0,  8}
///   AH  -> Reg{1,  8} (byte 1 of RAX)
struct Reg {
  std::uint32_t offset; // byte offset in the register address space
  Width width; // bit width
  std::string name; // an optional name to improve output
};

/// SSA temporary value, the registers need to
/// be loaded into temporary values to be used
/// acting as a memory location.
struct Temp {
  std::uint32_t id;
  Width width;
};

/// Any value usable as operand or destination
/// We use a variant as no expansion will be done
/// in the future.
using Value = std::variant<Const, Reg, Temp>;

/// For any type of the Value type, returns the
/// Width, we can use std::visit
[[nodiscard]] Width widthOf(const Value &v);

/// Opcode for the operations of IGNIL, we need
/// to have operations that provide information
/// about sign of the operands.
enum class Opcode : std::uint8_t {
  // Data movement
  COPY, // dst = srcs[0]
  LOAD, // dst = memory[srcs[0]] (width bytes)
  STORE, // memory[srcs[0]] = srcs[1] (no dst)

  // Arithmetic (sign-agnostic)
  ADD,
  SUB,
  NEG,
  MUL,

  // Arithmetic (sign-dependent)
  SDIV,
  UDIV,
  SMOD,
  UMOD,

  // Bitwise (sign-agnostic)
  AND,
  OR,
  XOR,
  NOT,
  SHL, // logical shift left
  SHR, // logical shift right (zero fill)
  SAR, // arithmetic shift right (sign fill)

  // Width conversion (IGNIL use explicit conversions)
  ZEXT, // zero-extend srcs[0] to dst width
  SEXT, // sign-extend srcs[0] to dst width
  TRUNC, // truncate    srcs[0] to dst width

  // Comparison - always produce a 1-bit result
  EQ, // ==
  NEQ, // !=
  // Unsigned comparison
  ULT, // <
  ULE, // <=
  UGT, // >
  UGE, // >=
  // Signed comparison
  SLT, // <
  SLE, // <=
  SGT, // >
  SGE, // >=

  // Control flow
  JMP, // unconditional - srcs[0] = target address
  CJMP, // srcs[0] = condition (1-bit)
  // srcs[1] = taken address
  // srcs[2] = not-taken address
  CALL, // srcs[0] = target address
  RET, // no operands

  // Symbolic unknown - models unresolvable inputs
  UNDEF,

  // Floating-point arithmetic (IEEE754 double, width always 64). Operands
  // and dst are the *same* raw-bit-pattern Const/Reg/Temp every other
  // opcode uses - floatness is decided by the operation, exactly like
  // SDIV/UDIV already decide signedness for the same underlying bits
  // rather than Value itself carrying a signed/unsigned tag. A lifter
  // reinterprets (bitcasts) a Reg/Temp/Const's bits as a double only when
  // an op here consumes it.
  //
  // Appended after UNDEF (not grouped with ADD/SUB/MUL above, where they
  // would read more naturally) rather than interleaved into the enum:
  // dragon_c.h's dragon_ignil_opcode_t and python/dragon.py's IGNILOpcode
  // both mirror this enum as hardcoded, value-for-value integer constants
  // (checked at compile time for a few spot values via static_assert in
  // dragon_c.cpp, but *not* exhaustively - a mid-enum insertion silently
  // shifts every later value with no compiler error to catch the ones the
  // static_asserts don't happen to cover). New opcodes always go at the
  // end for exactly this reason.
  FADD,
  FSUB,
  FMUL,
  FDIV,

  // Int <-> float conversion (signed only - no x86 scalar SSE2 instruction
  // exists for unsigned int<->double in the classic ISA this lifter
  // targets, so no UITOFP/FPTOUI pair is declared; add one if a real
  // target ever needs it).
  SITOFP, // dst(double, width 64) = (double)(signed)srcs[0]
  FPTOSI, // dst(int) = srcs[0], interpreted as double, truncated toward zero
};

/// Opcode representation, we have a structure
/// modeled by each one of the operations, this
/// one must be simple enough, and each instruction
/// in Assembly will be 1 to N IGNIL Op.
struct Op {
  // operation from enum
  Opcode opcode;

  // Destination - optional absent for STORE, JMP, CJMP, CALL, RET
  std::optional<Value> dst;

  // Source operands (count and semantics depend on opcode)
  std::vector<Value> srcs;

  // Address of the machine instruction that produced this Op.
  // Multiple Ops from the same machine instruction share
  // this address. Zero for synthetic Ops.
  std::uint64_t address{};
};

/// A block of IGNIL operations, this is a translation of an
/// assembly block, but the number of instructions may be bigger
/// since 1 assembly instruction may be translated to N IGNIL
/// instructions.
class Block {
  public:
  explicit Block(std::uint64_t startAddress);

  void append(Op op);

  [[nodiscard]] const std::vector<Op> &ops() const;
  [[nodiscard]] std::uint64_t startAddress() const;
  [[nodiscard]] bool empty() const;
  [[nodiscard]] size_t size() const;

  // Used for iteration
  [[nodiscard]] std::vector<Op>::const_iterator begin() const;
  [[nodiscard]] std::vector<Op>::const_iterator end() const;

  private:
  std::uint64_t startAddress_;
  std::vector<Op> ops_;
};

/// Utility functions that can be used for debugging purposes
[[nodiscard]] std::string_view opcodeName(Opcode op);
[[nodiscard]] std::string toString(const Value &v);
[[nodiscard]] std::string toString(const Op &op);
[[nodiscard]] std::string toString(const Block &block);

} // namespace dragon::ignil
