#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <dragon/Architecture.hpp>
#include <dragon/Configuration.hpp>

namespace dragon {

class Assembler {
  public:
  /// The assembler receives the architecture and the configuration to set up
  /// the internal LLVM assembler. Internally it contains the implementation
  /// of LLVM.
  Assembler(Architecture architecture, const Configuration &configuration);
  ~Assembler();

  Assembler(const Assembler &) = delete;
  Assembler &operator=(const Assembler &) = delete;

  Assembler(Assembler &&) noexcept;
  Assembler &operator=(Assembler &&) noexcept = default;

  /// Needed function to know if the assembler was properly created or not.
  [[nodiscard]] bool isValid() const noexcept;
  [[nodiscard]] Architecture architecture() const noexcept {
    return architecture_;
  }
  [[nodiscard]] const Configuration &configuration() const noexcept {
    return configuration_;
  }

  /// Assemble text -> raw bytes.
  /// Returns empty vector on failure.
  [[nodiscard]] std::vector<std::uint8_t> assemble(std::string_view text) const;

  private:
  struct Impl;

  Architecture architecture_;
  Configuration configuration_;
  std::unique_ptr<Impl> impl_;
};

} // namespace dragon
