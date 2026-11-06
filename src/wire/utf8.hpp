#pragma once
// Private to the wire module.
#include <cstddef>
#include <string_view>

namespace crpp::wire::detail {

// Length (1-4) of the valid UTF-8 sequence starting at s[pos], or 0 if the
// bytes there are not valid UTF-8 (overlong, surrogate, > U+10FFFF, truncated).
// Requires pos < s.size().
[[nodiscard]] constexpr std::size_t utf8_length(std::string_view s, std::size_t pos) noexcept {
  const auto b0 = static_cast<unsigned char>(s[pos]);
  if (b0 < 0x80) return 1;
  std::size_t n = 0;
  char32_t cp = 0;
  char32_t min = 0;
  if (b0 >= 0xC2 && b0 <= 0xDF) {
    n = 2; cp = b0 & 0x1Fu; min = 0x80;
  } else if ((b0 & 0xF0u) == 0xE0u) {
    n = 3; cp = b0 & 0x0Fu; min = 0x800;
  } else if (b0 >= 0xF0 && b0 <= 0xF4) {
    n = 4; cp = b0 & 0x07u; min = 0x10000;
  } else {
    return 0;
  }
  if (pos + n > s.size()) return 0;
  for (std::size_t i = 1; i < n; ++i) {
    const auto b = static_cast<unsigned char>(s[pos + i]);
    if ((b & 0xC0u) != 0x80u) return 0;
    cp = (cp << 6) | (b & 0x3Fu);
  }
  if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
  return n;
}

}  // namespace crpp::wire::detail
