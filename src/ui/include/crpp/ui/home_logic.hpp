#pragma once
// What the Home screen's buttons decide, apart from how it looks. No platform
// headers; wide strings are UTF-16 on the one platform the UI runs on today.
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace crpp::ui::home {

inline constexpr std::size_t code_length = 6;
inline constexpr const char* code_length_error = "구름 코드를 6자리로 입력해 주세요.";

// The characters ECMAScript's String.prototype.trim removes.
[[nodiscard]] constexpr bool is_trimmed(wchar_t c) {
  return (c >= 0x09 && c <= 0x0D) || c == 0x20 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) ||
         c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF;
}

// The code to send: trimmed and in upper case, and exactly six UTF-16 units. Which
// characters a code may hold is the server's to say, as it is upstream's.
[[nodiscard]] inline std::optional<std::wstring> normalize_code(std::wstring_view text) {
  while (!text.empty() && is_trimmed(text.front())) text.remove_prefix(1);
  while (!text.empty() && is_trimmed(text.back())) text.remove_suffix(1);
  if (text.size() != code_length) return std::nullopt;
  std::wstring code(text);
  for (wchar_t& c : code) {
    if (c >= L'a' && c <= L'z') c = static_cast<wchar_t>(c - L'a' + L'A');
  }
  return code;
}

// How to enter a Room.
struct Entry {
  enum class Kind : unsigned char { Random, Join, Resume };
  Kind kind = Kind::Random;
  std::string code, key;  // Join: code. Resume: both.
};

// A code goes back to the Seat held in that Room, if any. If the server no longer
// has that Seat, the resume falls back to joining.
[[nodiscard]] inline Entry entry_for_code(std::string code, std::optional<std::string> held_key) {
  if (held_key) return {Entry::Kind::Resume, std::move(code), std::move(*held_key)};
  return {Entry::Kind::Join, std::move(code), {}};
}

}  // namespace crpp::ui::home
