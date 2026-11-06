#include "json_reader.hpp"

#include <algorithm>
#include <limits>

#include "utf8.hpp"

namespace crpp::wire::detail {

namespace {

constexpr bool is_ws(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
constexpr bool is_alnum(char c) noexcept {
  return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

constexpr int hex_value(char c) noexcept {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

void Reader::fail(ParseError e) noexcept {
  if (failed_) return;
  failed_ = true;
  error_ = e;
}

void Reader::skip_ws() noexcept {
  while (pos_ < text_.size() && is_ws(text_[pos_])) ++pos_;
}

bool Reader::expect(char c, ParseError otherwise) noexcept {
  if (failed_) return false;
  skip_ws();
  if (pos_ >= text_.size()) { fail(ParseError::Syntax); return false; }
  if (text_[pos_] != c) { fail(otherwise); return false; }
  ++pos_;
  return true;
}

bool Reader::put(char c) noexcept {
  if (used_ >= scratch_.size()) { fail(ParseError::ScratchTooSmall); return false; }
  scratch_[used_++] = c;
  return true;
}

bool Reader::put_utf8(char32_t cp) noexcept {
  if (cp < 0x80) return put(static_cast<char>(cp));
  if (cp < 0x800) {
    return put(static_cast<char>(0xC0 | (cp >> 6))) && put(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  if (cp < 0x10000) {
    return put(static_cast<char>(0xE0 | (cp >> 12))) &&
           put(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))) &&
           put(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  return put(static_cast<char>(0xF0 | (cp >> 18))) &&
         put(static_cast<char>(0x80 | ((cp >> 12) & 0x3F))) &&
         put(static_cast<char>(0x80 | ((cp >> 6) & 0x3F))) &&
         put(static_cast<char>(0x80 | (cp & 0x3F)));
}

bool Reader::hex4(char32_t& out) noexcept {
  if (pos_ + 4 > text_.size()) { fail(ParseError::Syntax); return false; }
  char32_t v = 0;
  for (int i = 0; i < 4; ++i) {
    const int h = hex_value(text_[pos_ + static_cast<std::size_t>(i)]);
    if (h < 0) { fail(ParseError::Escape); return false; }
    v = (v << 4) | static_cast<char32_t>(h);
  }
  pos_ += 4;
  out = v;
  return true;
}

// pos_ is at the backslash.
bool Reader::read_escape() noexcept {
  ++pos_;
  if (pos_ >= text_.size()) { fail(ParseError::Syntax); return false; }
  const char e = text_[pos_++];
  switch (e) {
    case '"': return put('"');
    case '\\': return put('\\');
    case '/': return put('/');
    case 'b': return put('\b');
    case 'f': return put('\f');
    case 'n': return put('\n');
    case 'r': return put('\r');
    case 't': return put('\t');
    case 'u': {
      char32_t cp = 0;
      if (!hex4(cp)) return false;
      if (cp >= 0xD800 && cp <= 0xDBFF) {
        if (text_.substr(pos_, 2) != "\\u") { fail(ParseError::Escape); return false; }
        pos_ += 2;
        char32_t low = 0;
        if (!hex4(low)) return false;
        if (low < 0xDC00 || low > 0xDFFF) { fail(ParseError::Escape); return false; }
        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
      } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
        fail(ParseError::Escape);
        return false;
      }
      return put_utf8(cp);
    }
    default:
      fail(ParseError::Escape);
      return false;
  }
}

std::string_view Reader::string() noexcept {
  if (!expect('"', ParseError::Type)) return {};
  const std::size_t start = used_;
  for (;;) {
    if (pos_ >= text_.size()) { fail(ParseError::Syntax); return {}; }
    const char c = text_[pos_];
    const auto u = static_cast<unsigned char>(c);
    if (c == '"') { ++pos_; break; }
    if (u < 0x20) { fail(ParseError::Syntax); return {}; }
    if (c == '\\') {
      if (!read_escape()) return {};
      continue;
    }
    if (u < 0x80) {
      if (!put(c)) return {};
      ++pos_;
      continue;
    }
    const std::size_t n = utf8_length(text_, pos_);
    if (n == 0) { fail(ParseError::Utf8); return {}; }
    if (used_ + n > scratch_.size()) { fail(ParseError::ScratchTooSmall); return {}; }
    std::copy_n(text_.data() + pos_, n, scratch_.data() + used_);
    used_ += n;
    pos_ += n;
  }
  return std::string_view(scratch_.data() + start, used_ - start);
}

void Reader::begin_object() noexcept {
  if (!expect('{', ParseError::Type)) return;
  if (depth_ == max_depth) { fail(ParseError::Depth); return; }
  first_[depth_++] = true;
}

std::optional<std::string_view> Reader::next_key() noexcept {
  if (failed_) return std::nullopt;
  skip_ws();
  if (pos_ >= text_.size()) { fail(ParseError::Syntax); return std::nullopt; }
  if (text_[pos_] == '}') {
    ++pos_;
    --depth_;
    return std::nullopt;
  }
  if (!first_[depth_ - 1]) {
    if (text_[pos_] != ',') { fail(ParseError::Syntax); return std::nullopt; }
    ++pos_;
  }
  first_[depth_ - 1] = false;
  skip_ws();
  if (pos_ >= text_.size() || text_[pos_] != '"') { fail(ParseError::Syntax); return std::nullopt; }
  const std::size_t mark = used_;
  const std::string_view key = string();
  used_ = mark;  // the key is only valid until the next read
  if (failed_) return std::nullopt;
  if (!expect(':', ParseError::Syntax)) return std::nullopt;
  return key;
}

void Reader::begin_array() noexcept {
  if (!expect('[', ParseError::Type)) return;
  if (depth_ == max_depth) { fail(ParseError::Depth); return; }
  first_[depth_++] = true;
}

bool Reader::next_element() noexcept {
  if (failed_) return false;
  skip_ws();
  if (pos_ >= text_.size()) { fail(ParseError::Syntax); return false; }
  if (text_[pos_] == ']') {
    ++pos_;
    --depth_;
    return false;
  }
  if (!first_[depth_ - 1]) {
    if (text_[pos_] != ',') { fail(ParseError::Syntax); return false; }
    ++pos_;
  }
  first_[depth_ - 1] = false;
  return true;
}

bool Reader::match_literal(std::string_view literal) noexcept {
  if (text_.substr(pos_, literal.size()) != literal) return false;
  const std::size_t end = pos_ + literal.size();
  if (end < text_.size() && is_alnum(text_[end])) return false;
  pos_ = end;
  return true;
}

bool Reader::null() noexcept {
  if (failed_) return false;
  skip_ws();
  return match_literal("null");
}

bool Reader::boolean() noexcept {
  if (failed_) return false;
  skip_ws();
  if (match_literal("true")) return true;
  if (match_literal("false")) return false;
  fail(pos_ >= text_.size() ? ParseError::Syntax : ParseError::Type);
  return false;
}

// Integer only: a fraction or exponent is a Number error.
bool Reader::parse_integer(bool& negative, std::uint64_t& magnitude) noexcept {
  skip_ws();
  if (pos_ >= text_.size()) { fail(ParseError::Syntax); return false; }
  std::size_t p = pos_;
  negative = text_[p] == '-';
  if (negative) ++p;
  if (p >= text_.size() || !is_digit(text_[p])) {
    fail(negative || pos_ >= text_.size() ? ParseError::Number : ParseError::Type);
    return false;
  }
  if (text_[p] == '0' && p + 1 < text_.size() && is_digit(text_[p + 1])) {
    fail(ParseError::Number);
    return false;
  }
  std::uint64_t v = 0;
  while (p < text_.size() && is_digit(text_[p])) {
    const auto d = static_cast<std::uint64_t>(text_[p] - '0');
    if (v > (std::numeric_limits<std::uint64_t>::max() - d) / 10) {
      fail(ParseError::Number);
      return false;
    }
    v = v * 10 + d;
    ++p;
  }
  if (p < text_.size() && (text_[p] == '.' || text_[p] == 'e' || text_[p] == 'E')) {
    fail(ParseError::Number);
    return false;
  }
  pos_ = p;
  magnitude = v;
  return true;
}

std::uint64_t Reader::u64() noexcept {
  if (failed_) return 0;
  bool negative = false;
  std::uint64_t v = 0;
  if (!parse_integer(negative, v)) return 0;
  if (negative && v != 0) { fail(ParseError::Number); return 0; }
  return v;
}

std::int64_t Reader::i64() noexcept {
  if (failed_) return 0;
  bool negative = false;
  std::uint64_t v = 0;
  if (!parse_integer(negative, v)) return 0;
  constexpr auto max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  if (!negative) {
    if (v > max) { fail(ParseError::Number); return 0; }
    return static_cast<std::int64_t>(v);
  }
  if (v > max + 1) { fail(ParseError::Number); return 0; }
  return v == max + 1 ? std::numeric_limits<std::int64_t>::min()
                      : -static_cast<std::int64_t>(v);
}

void Reader::skip_number() noexcept {
  std::size_t p = pos_;
  const auto digits = [&] {
    if (p >= text_.size() || !is_digit(text_[p])) return false;
    while (p < text_.size() && is_digit(text_[p])) ++p;
    return true;
  };
  if (p < text_.size() && text_[p] == '-') ++p;
  if (p < text_.size() && text_[p] == '0') {
    ++p;
    if (p < text_.size() && is_digit(text_[p])) { fail(ParseError::Number); return; }
  } else if (!digits()) {
    fail(ParseError::Number);
    return;
  }
  if (p < text_.size() && text_[p] == '.') {
    ++p;
    if (!digits()) { fail(ParseError::Number); return; }
  }
  if (p < text_.size() && (text_[p] == 'e' || text_[p] == 'E')) {
    ++p;
    if (p < text_.size() && (text_[p] == '+' || text_[p] == '-')) ++p;
    if (!digits()) { fail(ParseError::Number); return; }
  }
  pos_ = p;
}

void Reader::skip() noexcept {
  if (failed_) return;
  skip_ws();
  if (pos_ >= text_.size()) { fail(ParseError::Syntax); return; }
  switch (text_[pos_]) {
    case '{':
      begin_object();
      while (next_key()) skip();
      return;
    case '[':
      begin_array();
      while (next_element()) skip();
      return;
    case '"': {
      const std::size_t mark = used_;
      (void)string();
      used_ = mark;
      return;
    }
    case 't':
      if (!match_literal("true")) fail(ParseError::Syntax);
      return;
    case 'f':
      if (!match_literal("false")) fail(ParseError::Syntax);
      return;
    case 'n':
      if (!match_literal("null")) fail(ParseError::Syntax);
      return;
    default:
      skip_number();
      return;
  }
}

void Reader::finish() noexcept {
  if (failed_) return;
  skip_ws();
  if (pos_ != text_.size()) fail(ParseError::TrailingData);
}

}  // namespace crpp::wire::detail
