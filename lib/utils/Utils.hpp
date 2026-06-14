#pragma once

#include <cstdint>
#include <format>
#include <string>

namespace dragon::utils {

/// Convert a uint64_t address to a hex string
/// e.g. 0x4010a0 → "4010a0"
[[nodiscard]] inline std::string toHex(std::uint64_t value) {
  return std::format("{:x}", value);
}

/// With prefix, e.g. 0x4010a0 → "0x4010a0"
[[nodiscard]] inline std::string toHexPrefixed(std::uint64_t value) {
  return std::format("0x{:x}", value);
}

template<class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

} // namespace dragon::utils
