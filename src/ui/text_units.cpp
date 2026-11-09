#include "crpp/ui/text_units.hpp"

#include <algorithm>
#include <cstdint>
#include <cwctype>

namespace crpp::ui {

namespace {

bool is_low_surrogate(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

// What a word is made of. A word is a run of one kind; spaces between words belong to none,
// and a line break stops every run.
enum class Kind : std::uint8_t { Space, Break, Word, Mark };

Kind kind_of(wchar_t c) {
  if (c == L'\n') return Kind::Break;
  if (std::iswspace(c) || c == 0x3000) return Kind::Space;
  if (c < 0x80) return std::iswalnum(c) || c == L'_' ? Kind::Word : Kind::Mark;
  // Punctuation outside ASCII: general, CJK and full-width. Letters of any script, Hangul
  // among them, and emoji are words, as are the joiners that make several emoji one.
  if (c == 0x200C || c == 0x200D) return Kind::Word;
  if ((c >= 0x2000 && c <= 0x206F) || (c >= 0x3001 && c <= 0x303F) || (c >= 0xFF01 && c <= 0xFF0F) ||
      (c >= 0xFF1A && c <= 0xFF20) || (c >= 0xFF3B && c <= 0xFF40) || (c >= 0xFF5B && c <= 0xFF65)) {
    return Kind::Mark;
  }
  return Kind::Word;
}

}  // namespace

std::size_t next_word(std::wstring_view s, std::size_t pos) {
  if (pos >= s.size()) return s.size();
  const Kind k = kind_of(s[pos]);
  if (k == Kind::Break) return pos + 1;
  if (k != Kind::Space) {
    while (pos < s.size() && kind_of(s[pos]) == k) ++pos;
  }
  while (pos < s.size() && kind_of(s[pos]) == Kind::Space) ++pos;
  return pos;
}

std::size_t previous_word(std::wstring_view s, std::size_t pos) {
  pos = std::min(pos, s.size());
  while (pos > 0 && kind_of(s[pos - 1]) == Kind::Space) --pos;
  if (pos == 0) return 0;
  const Kind k = kind_of(s[pos - 1]);
  if (k == Kind::Break) return pos - 1;
  while (pos > 0 && kind_of(s[pos - 1]) == k) --pos;
  return pos;
}

std::pair<std::size_t, std::size_t> unit_at(std::wstring_view s, std::size_t pos, int clicks) {
  pos = std::min(pos, s.size());
  if (clicks <= 1 || s.empty()) return {pos, pos};
  if (clicks >= 3) {
    const std::size_t before = pos == 0 ? std::wstring_view::npos : s.rfind(L'\n', pos - 1);
    const std::size_t end = s.find(L'\n', pos);
    return {before == std::wstring_view::npos ? 0 : before + 1, end == std::wstring_view::npos ? s.size() : end};
  }
  // The run the character at pos belongs to; at the end, the one before it.
  std::size_t at = pos < s.size() ? pos : pos - 1;
  if (is_low_surrogate(s[at]) && at > 0) --at;
  const Kind k = kind_of(s[at]);
  if (k == Kind::Break) return {at, at};
  std::size_t lo = at, hi = at;
  while (lo > 0 && kind_of(s[lo - 1]) == k) --lo;
  while (hi < s.size() && kind_of(s[hi]) == k) ++hi;
  return {lo, hi};
}

}  // namespace crpp::ui
