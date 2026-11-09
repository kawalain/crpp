#pragma once
// How a pack, a Theme, a locale and a version may be written, and what a user may be shown as a
// name. A pack's own files and a Repository's catalog (ADR 0008) both say these things of the same
// packs, so both check them here: what one takes the other takes.
#include <algorithm>
#include <cstddef>
#include <string_view>

namespace crpp::skin {

inline constexpr std::size_t max_id_length = 32;
inline constexpr std::size_t max_name_bytes = 64;

// A pack's or Theme's id, and a locale's: 1 to 32 of a-z, 0-9, - and _. Short enough that a pack
// id with a Repository's prefix stays within what the server takes for a Sound pack.
[[nodiscard]] constexpr bool is_pack_id(std::string_view s) {
  return !s.empty() && s.size() <= max_id_length && std::ranges::all_of(s, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
  });
}

// Lowercase hex of this length: one text for one value, so nothing signed can vary.
[[nodiscard]] constexpr bool is_lower_hex(std::string_view s, std::size_t length) {
  return s.size() == length && std::ranges::all_of(s, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// Digits with no leading zero, `parts` of them between dots.
[[nodiscard]] constexpr bool is_dotted(std::string_view s, std::size_t parts) {
  for (std::size_t i = 0; i < parts; ++i) {
    const std::size_t dot = s.find('.');
    const std::string_view part = s.substr(0, dot);
    if (part.empty() || part.size() > 9 || (part.size() > 1 && part[0] == '0') || !std::ranges::all_of(part, [](char c) { return c >= '0' && c <= '9'; })) return false;
    if (i + 1 == parts) return dot == std::string_view::npos;
    if (dot == std::string_view::npos) return false;
    s.remove_prefix(dot + 1);
  }
  return false;
}

// A pack's version: 0.y.z.
[[nodiscard]] constexpr bool is_version(std::string_view s) { return s.starts_with("0.") && is_dotted(s, 3); }
// What a pin takes of a parent's version: 0.y for any 0.y.z, or 0.y.z for that one.
[[nodiscard]] constexpr bool is_version_pin(std::string_view s) { return s.starts_with("0.") && (is_dotted(s, 2) || is_dotted(s, 3)); }

// What a user calls a pack or a Theme: 1 to 64 bytes, no space at either end, and nothing that
// is not shown as it is written. The marks that turn the direction of text are left out with the
// control characters: a name is read in a question about trusting whoever wrote it.
[[nodiscard]] constexpr bool is_shown_name(std::string_view s) {
  if (s.empty() || s.size() > max_name_bytes || s.front() == ' ' || s.back() == ' ') return false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x20 || c == 0x7F) return false;
    // U+202A to U+202E are E2 80 AA..AE, and U+2066 to U+2069 are E2 81 A6..A9.
    if (c == 0xE2 && i + 2 < s.size()) {
      const auto second = static_cast<unsigned char>(s[i + 1]), third = static_cast<unsigned char>(s[i + 2]);
      if ((second == 0x80 && third >= 0xAA && third <= 0xAE) || (second == 0x81 && third >= 0xA6 && third <= 0xA9)) return false;
    }
  }
  return true;
}

}  // namespace crpp::skin
