#include <dragon/Architecture.hpp>

constexpr std::string dragon::ArchitectureName(Architecture architecture) {
  switch (architecture) {
    case Architecture::X86_32:
      return "x86_32";
    case Architecture::X86_64:
      return "x86_64";
    case Architecture::ARM32:
      return "arm32";
    case Architecture::ARM64:
      return "arm64";
    case Architecture::RISCV32:
      return "riscv32";
    case Architecture::RISCV64:
      return "riscv64";
    default:
      return "unknown";
  }
}
