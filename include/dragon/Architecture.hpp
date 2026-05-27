#pragma once
#include <cstdint>
#include <string>

namespace dragon {
/// Enum indicating the architectures supported by Dragon
/// the idea is keeping here those supported ones, and not
/// allowing the user to create other ones. These architectures
/// must be provided to other objects like the Disassembler,
/// Assembler, etc...
enum class Architecture : uint8_t {
  Unknown = 0, ///< Provided value Unknown
  // X86 family
  X86_32, ///< x86 architecture 32 bits
  X86_64, ///< x86 architecture 64 bits
  // ARM family
  ARM32, ///< arm architecture 32 bits
  ARM64, ///< arm architecture 64 bits
  // RISCV family
  RISCV32, ///< riscv architecture 32 bits
  RISCV64, ///< riscv architecture 64 bits
};

/// Get a string representation of the previous architectures.
constexpr std::string ArchitectureName(Architecture architecture);

} // namespace dragon
