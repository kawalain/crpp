#pragma once
// Text into a buffer that is already there, for a crash handler that must not allocate. What does
// not fit is dropped, and full() says so.
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace crpp::crash {

class Writer {
 public:
  explicit Writer(std::span<char> to) noexcept : to_(to) {}

  Writer& text(std::string_view text) noexcept;
  Writer& ch(char c) noexcept { return text(std::string_view(&c, 1)); }
  // At least `digits` of them, without "0x".
  Writer& hex(std::uint64_t value, int digits = 1, bool upper = false) noexcept;
  // At least `digits`, with leading zeros.
  Writer& dec(std::uint64_t value, int digits = 1) noexcept;
  // Milliseconds as seconds with one decimal: 12345 is "12.3".
  Writer& seconds(std::uint64_t ms) noexcept;

  [[nodiscard]] std::string_view written() const noexcept { return {to_.data(), used_}; }
  [[nodiscard]] bool full() const noexcept { return full_; }

 private:
  std::span<char> to_;
  std::size_t used_ = 0;
  bool full_ = false;
};

}  // namespace crpp::crash
