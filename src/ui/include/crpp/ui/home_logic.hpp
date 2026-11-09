#pragma once
// What the Home screen's buttons decide, apart from how it looks. No platform
// headers; wide strings are UTF-16 on the one platform the UI runs on today.
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace crpp::ui::home {

inline constexpr std::size_t code_length = 6;

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

// The search button: a held Seat is offered first, public or not.
enum class FindStep : unsigned char { AskReturn, Random };
[[nodiscard]] constexpr FindStep find_step(bool seat_held) { return seat_held ? FindStep::AskReturn : FindStep::Random; }

struct HeldSeat {
  std::string code, key;
};

// "돌아가기": the Seat may have expired while the question was open.
[[nodiscard]] inline Entry entry_for_return(std::optional<HeldSeat> latest) {
  if (!latest) return {};
  return {Entry::Kind::Resume, std::move(latest->code), std::move(latest->key)};
}

// A day the greeting is about instead of the hour, by the local calendar.
enum class SpecialDay : unsigned char { None, NewYear, AprilFools, Birthday, Christmas };

// Cloudroom++'s own: v0.1.0 was released on 7 November 2026.
inline constexpr int birthday_month = 11, birthday_day = 7;

[[nodiscard]] constexpr SpecialDay special_day(int month, int day) {
  if (month == 1 && day == 1) return SpecialDay::NewYear;
  if (month == 4 && day == 1) return SpecialDay::AprilFools;
  if (month == birthday_month && day == birthday_day) return SpecialDay::Birthday;
  if (month == 12 && (day == 24 || day == 25)) return SpecialDay::Christmas;
  return SpecialDay::None;
}

// How many public Rooms are open, from GET /health's plain text: "ok", "rooms=<n>" (the private
// ones) and "public_rooms=<n>", a line each. Nothing for an answer without that line.
[[nodiscard]] inline std::optional<int> public_rooms_in_health(std::string_view body) {
  constexpr std::string_view key = "public_rooms=";
  while (!body.empty()) {
    const std::size_t end = body.find('\n');
    std::string_view line = body.substr(0, end);
    body = end == std::string_view::npos ? std::string_view{} : body.substr(end + 1);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (!line.starts_with(key)) continue;
    line.remove_prefix(key.size());
    if (line.empty() || line.size() > 9) return std::nullopt;
    int n = 0;
    for (const char c : line) {
      if (c < '0' || c > '9') return std::nullopt;
      n = n * 10 + (c - '0');
    }
    return n;
  }
  return std::nullopt;
}

}  // namespace crpp::ui::home
