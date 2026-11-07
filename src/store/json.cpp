#include "crpp/store/json.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>

namespace crpp::store {

namespace {

constexpr std::size_t max_depth = 64;

bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

void append_utf8(std::string& out, char32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// Length of the valid UTF-8 sequence at the start of s, or 0.
std::size_t utf8_sequence(std::string_view s) noexcept {
  const auto at = [&](std::size_t i) { return static_cast<unsigned char>(s[i]); };
  const auto tail = [&](std::size_t i, unsigned low = 0x80, unsigned high = 0xBF) {
    return i < s.size() && at(i) >= low && at(i) <= high;
  };
  const unsigned lead = at(0);
  if (lead < 0x80) return 1;
  if (lead >= 0xC2 && lead <= 0xDF) return tail(1) ? 2 : 0;
  if (lead == 0xE0) return tail(1, 0xA0) && tail(2) ? 3 : 0;
  if (lead == 0xED) return tail(1, 0x80, 0x9F) && tail(2) ? 3 : 0;
  if (lead >= 0xE1 && lead <= 0xEF) return tail(1) && tail(2) ? 3 : 0;
  if (lead == 0xF0) return tail(1, 0x90) && tail(2) && tail(3) ? 4 : 0;
  if (lead >= 0xF1 && lead <= 0xF3) return tail(1) && tail(2) && tail(3) ? 4 : 0;
  if (lead == 0xF4) return tail(1, 0x80, 0x8F) && tail(2) && tail(3) ? 4 : 0;
  return 0;
}

void dump_string(std::string& out, std::string_view s) {
  constexpr char hex[] = "0123456789abcdef";
  out += '"';
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out += "\\u00";
          out += hex[(static_cast<unsigned char>(c) >> 4) & 0xF];
          out += hex[static_cast<unsigned char>(c) & 0xF];
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

}  // namespace

// ------------------------------------------------------------------ parser

class JsonParser {
 public:
  explicit JsonParser(std::string_view text) noexcept : text_(text) {}

  std::expected<Json, JsonError> run(JsonPosition* where) {
    Json value = parse_value(0);
    skip_ws();
    if (!failed_ && pos_ != text_.size()) fail(JsonError::Syntax);
    if (failed_) {
      if (where) *where = position();
      return std::unexpected(error_);
    }
    return value;
  }

 private:
  void fail(JsonError e) noexcept {
    if (!failed_) error_ = e;
    failed_ = true;
  }
  [[nodiscard]] JsonPosition position() const noexcept {
    JsonPosition at;
    for (const char c : text_.substr(0, pos_)) {
      if (c == '\n') {
        ++at.line;
        at.column = 1;
      } else {
        ++at.column;
      }
    }
    return at;
  }
  // Space and comments.
  void skip_ws() noexcept {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
        const std::size_t end = text_.find('\n', pos_);
        pos_ = end == std::string_view::npos ? text_.size() : end;
      } else if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '*') {
        const std::size_t end = text_.find("*/", pos_ + 2);
        if (end == std::string_view::npos) {
          fail(JsonError::Syntax);
          return;
        }
        pos_ = end + 2;
      } else {
        return;
      }
    }
  }
  [[nodiscard]] bool consume(char c) noexcept {
    if (pos_ < text_.size() && text_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }
  bool literal(std::string_view word) noexcept {
    if (text_.substr(pos_, word.size()) != word) {
      fail(JsonError::Syntax);
      return false;
    }
    pos_ += word.size();
    return true;
  }

  Json parse_value(std::size_t depth) {
    if (depth > max_depth) {
      fail(JsonError::TooDeep);
      return {};
    }
    skip_ws();
    if (pos_ >= text_.size()) {
      fail(JsonError::Syntax);
      return {};
    }
    switch (text_[pos_]) {
      case '{': return parse_object(depth);
      case '[': return parse_array(depth);
      case '"': return Json(parse_string());
      case 't': return literal("true") ? Json(true) : Json{};
      case 'f': return literal("false") ? Json(false) : Json{};
      case 'n': literal("null"); return {};
      default: return parse_number();
    }
  }

  Json parse_object(std::size_t depth) {
    ++pos_;
    Json result = Json::object();
    skip_ws();
    if (consume('}')) return result;
    while (!failed_) {
      skip_ws();
      if (pos_ >= text_.size() || text_[pos_] != '"') {
        fail(JsonError::Syntax);
        break;
      }
      std::string key = parse_string();
      skip_ws();
      if (!consume(':')) {
        fail(JsonError::Syntax);
        break;
      }
      Json value = parse_value(depth + 1);
      if (failed_) break;
      result.set(key, std::move(value));  // a repeated key: the last one wins
      skip_ws();
      if (consume('}')) return result;
      if (!consume(',')) fail(JsonError::Syntax);
      skip_ws();
      if (!failed_ && consume('}')) return result;
    }
    return {};
  }

  Json parse_array(std::size_t depth) {
    ++pos_;
    Json::Array items;
    skip_ws();
    if (consume(']')) return Json(std::move(items));
    while (!failed_) {
      items.push_back(parse_value(depth + 1));
      if (failed_) break;
      skip_ws();
      if (consume(']')) return Json(std::move(items));
      if (!consume(',')) fail(JsonError::Syntax);
      skip_ws();
      if (!failed_ && consume(']')) return Json(std::move(items));
    }
    return {};
  }

  Json parse_number() {
    const std::size_t start = pos_;
    (void)consume('-');
    if (consume('0')) {
      // No further integer digits after a leading zero.
    } else if (pos_ < text_.size() && text_[pos_] >= '1' && text_[pos_] <= '9') {
      digits();
    } else {
      fail(JsonError::Syntax);
      return {};
    }
    if (consume('.') && !digits()) return {};
    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
      if (!consume('+')) (void)consume('-');
      if (!digits()) return {};
    }
    Json result;
    result.value_ = Json::Number{std::string(text_.substr(start, pos_ - start))};
    return result;
  }
  bool digits() noexcept {
    const std::size_t start = pos_;
    while (pos_ < text_.size() && is_digit(text_[pos_])) ++pos_;
    if (pos_ == start) fail(JsonError::Syntax);
    return pos_ != start;
  }

  bool hex4(char32_t& out) noexcept {
    if (text_.size() - pos_ < 4) return false;
    out = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[pos_++];
      const unsigned digit = is_digit(c) ? static_cast<unsigned>(c - '0')
                             : c >= 'a' && c <= 'f' ? static_cast<unsigned>(c - 'a') + 10
                             : c >= 'A' && c <= 'F' ? static_cast<unsigned>(c - 'A') + 10
                                                    : 16;
      if (digit == 16) return false;
      out = (out << 4) | digit;
    }
    return true;
  }

  std::string parse_string() {
    ++pos_;
    std::string out;
    while (!failed_) {
      if (pos_ >= text_.size()) {
        fail(JsonError::Syntax);
        break;
      }
      const char c = text_[pos_];
      if (c == '"') {
        ++pos_;
        return out;
      }
      if (c == '\\') {
        ++pos_;
        parse_escape(out);
      } else if (static_cast<unsigned char>(c) < 0x20) {
        fail(JsonError::Syntax);
      } else {
        const std::size_t n = utf8_sequence(text_.substr(pos_));
        if (n == 0) {
          fail(JsonError::BadUtf8);
          break;
        }
        out.append(text_, pos_, n);
        pos_ += n;
      }
    }
    return {};
  }

  void parse_escape(std::string& out) {
    if (pos_ >= text_.size()) {
      fail(JsonError::Syntax);
      return;
    }
    const char c = text_[pos_++];
    switch (c) {
      case '"': case '\\': case '/': out += c; return;
      case 'b': out += '\b'; return;
      case 'f': out += '\f'; return;
      case 'n': out += '\n'; return;
      case 'r': out += '\r'; return;
      case 't': out += '\t'; return;
      case 'u': break;
      default: fail(JsonError::Syntax); return;
    }
    char32_t cp = 0;
    if (!hex4(cp)) {
      fail(JsonError::Syntax);
      return;
    }
    if (cp >= 0xD800 && cp <= 0xDBFF) {
      char32_t low = 0;
      if (!consume('\\') || !consume('u') || !hex4(low) || low < 0xDC00 || low > 0xDFFF) {
        fail(JsonError::BadUtf8);
        return;
      }
      cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
      fail(JsonError::BadUtf8);
      return;
    }
    append_utf8(out, cp);
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  bool failed_ = false;
  JsonError error_ = JsonError::Syntax;
};

// ------------------------------------------------------------------- value

Json::Json(Array value) : value_(std::move(value)) {}
Json::Json(Object value) : value_(std::move(value)) {}
Json Json::object() { return Json(Object{}); }
Json Json::array() { return Json(Array{}); }

std::expected<Json, JsonError> Json::parse(std::string_view text, JsonPosition* where) { return JsonParser(text).run(where); }

std::optional<bool> Json::as_bool() const noexcept {
  if (const auto* v = std::get_if<bool>(&value_)) return *v;
  return std::nullopt;
}

std::optional<std::int64_t> Json::as_i64() const noexcept {
  const auto* v = std::get_if<Number>(&value_);
  if (!v) return std::nullopt;
  std::int64_t out = 0;
  const char* const end = v->text.data() + v->text.size();
  const auto [ptr, ec] = std::from_chars(v->text.data(), end, out);
  if (ec != std::errc{} || ptr != end) return std::nullopt;
  return out;
}

std::optional<std::string_view> Json::as_string() const noexcept {
  if (const auto* v = std::get_if<std::string>(&value_)) return std::string_view(*v);
  return std::nullopt;
}

const Json::Array* Json::as_array() const noexcept { return std::get_if<Array>(&value_); }
Json::Array* Json::as_array() noexcept { return std::get_if<Array>(&value_); }
const Json::Object* Json::as_object() const noexcept { return std::get_if<Object>(&value_); }

const Json* Json::find(std::string_view key) const noexcept {
  const auto* object = std::get_if<Object>(&value_);
  if (!object) return nullptr;
  const auto it = std::find_if(object->begin(), object->end(), [&](const Member& m) { return m.key == key; });
  return it == object->end() ? nullptr : &it->value;
}

Json* Json::find(std::string_view key) noexcept { return const_cast<Json*>(std::as_const(*this).find(key)); }

void Json::set(std::string_view key, Json value) {
  if (Json* existing = find(key)) {
    *existing = std::move(value);
    return;
  }
  if (!is_object()) value_ = Object{};
  std::get<Object>(value_).push_back(Member{std::string(key), std::move(value)});
}

bool Json::erase(std::string_view key) { return take(key).has_value(); }

std::optional<Json> Json::take(std::string_view key) {
  auto* object = std::get_if<Object>(&value_);
  if (!object) return std::nullopt;
  const auto it = std::find_if(object->begin(), object->end(), [&](const Member& m) { return m.key == key; });
  if (it == object->end()) return std::nullopt;
  Json value = std::move(it->value);
  object->erase(it);
  return value;
}

bool operator==(const Json& a, const Json& b) { return a.value_ == b.value_; }

// ---------------------------------------------------------------- printing

namespace {

void dump_value(std::string& out, const Json& value, bool pretty, std::size_t indent) {
  const auto line = [&](std::size_t level) {
    if (!pretty) return;
    out += '\n';
    out.append(level * 2, ' ');
  };
  if (value.is_null()) {
    out += "null";
  } else if (const auto b = value.as_bool()) {
    out += *b ? "true" : "false";
  } else if (const auto s = value.as_string()) {
    dump_string(out, *s);
  } else if (const auto* array = value.as_array()) {
    out += '[';
    bool first = true;
    for (const Json& item : *array) {
      if (!first) out += ',';
      first = false;
      line(indent + 1);
      dump_value(out, item, pretty, indent + 1);
    }
    if (!first) line(indent);
    out += ']';
  } else if (const auto* object = value.as_object()) {
    out += '{';
    bool first = true;
    for (const Json::Member& member : *object) {
      if (!first) out += ',';
      first = false;
      line(indent + 1);
      dump_string(out, member.key);
      out += pretty ? ": " : ":";
      dump_value(out, member.value, pretty, indent + 1);
    }
    if (!first) line(indent);
    out += '}';
  } else {
    out += value.number_text();
  }
}

}  // namespace

std::string Json::dump(bool pretty) const {
  std::string out;
  dump_value(out, *this, pretty, 0);
  return out;
}

}  // namespace crpp::store
