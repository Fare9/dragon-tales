#pragma once

#include <format>
#include <iostream>

namespace dragon::internal {

class Logger {
  public:
  explicit Logger(bool enabled = false) : enabled_(enabled) {}

  template<typename... Args>
  void log(std::format_string<Args...> fmt, Args &&...args) const {
    if (!enabled_)
      return;
    std::cerr << "[dragon] " << std::format(fmt, std::forward<Args>(args)...)
              << '\n';
  }

  bool isEnabled() const noexcept { return enabled_; }

  private:
  bool enabled_;
};

} // namespace dragon::internal
