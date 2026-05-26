#pragma once
#include <cstdint>

namespace dragon {

/// Syntax representation for the assembly instructions
/// this is part of the configuration to choose
enum class Syntax : std::uint8_t {
  INTEL = 0, ///< default, used by most of RE tools
  ATT, ///< AT&T style, used by GAS
};

/// Strategy used for the disassembly of the instructions in
/// the disassembler, for the moment, let's keep LINEAR_SWEEP
/// and RECURSIVE_TRAVERSAL. Soon, we can add an approach for
/// applying a mix to fill gaps
enum class DisassemblyStrategy : std::uint8_t {
  LINEAR_SWEEP = 0,       ///< Go from the first to the last bytes disassembling instructions.
  RECURSIVE_TRAVERSAL,    ///< Follow the control-flow in order to disassemble instructions.
};

/// Configuration class for different parts of the tool
/// Disassembler will use Syntax (probably same for the
/// Assembler).
struct Configuration {
  Syntax syntax = Syntax::INTEL;
  DisassemblyStrategy disassemblyStrategy = DisassemblyStrategy::LINEAR_SWEEP;
};

} // namespace dragon
