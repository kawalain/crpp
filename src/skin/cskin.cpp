#include "crpp/skin/cskin.hpp"

#include <algorithm>
#include <optional>
#include <vector>

#include "keys.hpp"

namespace crpp::skin {

namespace {

using store::Json;

constexpr int max_depth = 32;

enum class Tok : std::uint8_t { End, Newline, Ident, Number, String, Color, At, Punct };

struct Token {
  Tok type = Tok::End;
  std::string_view text;  // a String's without its quotes; an At's without the @; a Number's without its unit
  char unit = 0;          // a Number's: '%', 'm' for ms, or 0
  std::size_t begin = 0, end = 0;
  std::size_t line = 1, column = 1;
  std::size_t indent = 0;  // the indentation of the line it is on; a Newline's is the next line's
  bool space_before = false;
};

[[nodiscard]] bool name_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
[[nodiscard]] bool digit(char c) { return c >= '0' && c <= '9'; }
[[nodiscard]] bool name_char(char c) { return name_start(c) || digit(c); }
[[nodiscard]] bool hex(char c) { return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

class Lexer {
 public:
  explicit Lexer(std::string_view s) : s_(s) {}

  std::expected<std::vector<Token>, SourceError> run() {
    indent_ = measure_indent(0);
    at_ = indent_;
    column_ = indent_ + 1;
    while (true) {
      bool space = false;
      while (at_ < s_.size()) {
        const char c = s_[at_];
        if (c == ' ' || c == '\t' || c == '\r') {
          space = true;
          advance(1);
        } else if (c == '/' && at_ + 1 < s_.size() && s_[at_ + 1] == '/') {
          while (at_ < s_.size() && s_[at_] != '\n') advance(1);
        } else if (c == '\n') {
          space = true;
          newline();
        } else {
          break;
        }
      }
      if (at_ >= s_.size()) break;
      if (!token(space)) return std::unexpected(error_);
    }
    if (!out_.empty() && out_.back().type == Tok::Newline) out_.pop_back();
    out_.push_back(make(Tok::End, at_, true));
    return std::move(out_);
  }

 private:
  // How far in the line at `from` starts, skipping lines that hold only space or a comment.
  std::size_t measure_indent(std::size_t from) const {
    std::size_t n = 0;
    while (from + n < s_.size() && (s_[from + n] == ' ' || s_[from + n] == '\t')) ++n;
    return n;
  }

  void advance(std::size_t n) {
    at_ += n;
    column_ += n;
  }

  void newline() {
    ++at_;
    ++line_;
    column_ = 1;
    indent_ = measure_indent(at_);
    if (depth_ > 0) return;  // inside ( ), a line break is only space
    if (!out_.empty() && out_.back().type != Tok::Newline) out_.push_back(make(Tok::Newline, at_ - 1, false));
    if (!out_.empty() && out_.back().type == Tok::Newline) out_.back().indent = indent_;
  }

  Token make(Tok type, std::size_t begin, bool space) const {
    Token t;
    t.type = type;
    t.begin = begin;
    t.end = at_;
    t.line = line_;
    t.column = column_;
    t.indent = indent_;
    t.space_before = space;
    return t;
  }

  // Said where the token it is reading began.
  bool fail(std::string what) {
    error_ = {line_, start_column_, std::move(what)};
    return false;
  }

  bool token(bool space) {
    const std::size_t begin = at_;
    const std::size_t column = column_;
    start_column_ = column_;
    const auto push = [&](Tok type, std::string_view text, char unit = 0) {
      Token t = make(type, begin, space);
      t.column = column;
      t.text = text;
      t.unit = unit;
      out_.push_back(t);
      return true;
    };
    const char c = s_[at_];
    if (name_start(c)) {
      while (at_ < s_.size() && name_char(s_[at_])) advance(1);
      return push(Tok::Ident, s_.substr(begin, at_ - begin));
    }
    if (digit(c) || (c == '.' && at_ + 1 < s_.size() && digit(s_[at_ + 1]))) {
      while (at_ < s_.size() && digit(s_[at_])) advance(1);
      if (at_ < s_.size() && s_[at_] == '.') {
        advance(1);
        while (at_ < s_.size() && digit(s_[at_])) advance(1);
      }
      if (at_ < s_.size() && (s_[at_] == 'e' || s_[at_] == 'E') && at_ + 1 < s_.size() &&
          (digit(s_[at_ + 1]) || ((s_[at_ + 1] == '-' || s_[at_ + 1] == '+') && at_ + 2 < s_.size() && digit(s_[at_ + 2])))) {
        advance(2);
        while (at_ < s_.size() && digit(s_[at_])) advance(1);
      }
      const std::string_view digits = s_.substr(begin, at_ - begin);
      char unit = 0;
      if (at_ < s_.size() && s_[at_] == '%') {
        advance(1);
        unit = '%';
      } else if (s_.substr(at_, 2) == "ms" && (at_ + 2 >= s_.size() || !name_char(s_[at_ + 2]))) {
        advance(2);
        unit = 'm';
      }
      if (at_ < s_.size() && name_char(s_[at_])) return fail("a number runs into a name");
      return push(Tok::Number, digits, unit);
    }
    if (c == '"') {
      advance(1);
      while (at_ < s_.size() && s_[at_] != '"') {
        if (s_[at_] == '\n') return fail("a text has no closing \"");
        advance(s_[at_] == '\\' && at_ + 1 < s_.size() ? 2 : 1);
      }
      if (at_ >= s_.size()) return fail("a text has no closing \"");
      advance(1);
      return push(Tok::String, s_.substr(begin + 1, at_ - begin - 2));
    }
    if (c == '#') {
      advance(1);
      while (at_ < s_.size() && hex(s_[at_])) advance(1);
      const std::size_t n = at_ - begin - 1;
      if ((n != 6 && n != 8) || (at_ < s_.size() && name_char(s_[at_]))) return fail("a colour is # and 6 or 8 hex digits");
      return push(Tok::Color, s_.substr(begin, at_ - begin));
    }
    if (c == '@') {
      advance(1);
      if (at_ >= s_.size() || !name_start(s_[at_])) return fail("@ needs a name");
      while (at_ < s_.size() && name_char(s_[at_])) advance(1);
      if (at_ + 1 < s_.size() && s_[at_] == '.' && name_start(s_[at_ + 1])) {
        advance(1);
        while (at_ < s_.size() && name_char(s_[at_])) advance(1);
      }
      return push(Tok::At, s_.substr(begin + 1, at_ - begin - 1));
    }
    static constexpr std::string_view two[] = {"<=", ">=", "==", "!=", "&&", "||"};
    for (const std::string_view op : two) {
      if (s_.substr(at_, 2) == op) {
        advance(2);
        return push(Tok::Punct, op);
      }
    }
    if (c == '/' && at_ + 1 < s_.size() && s_[at_ + 1] == '*') return fail("comments are // to the end of the line");
    if (std::string_view("(){},=:?+-*/<>!.").find(c) != std::string_view::npos) {
      if (c == '(') ++depth_;
      if (c == ')' && depth_ > 0) --depth_;
      advance(1);
      return push(Tok::Punct, s_.substr(begin, 1));
    }
    return fail(std::string("unexpected \"") + c + "\"");
  }

  std::string_view s_;
  std::size_t at_ = 0, line_ = 1, column_ = 1, indent_ = 0, start_column_ = 1;
  int depth_ = 0;
  std::vector<Token> out_;
  SourceError error_;
};

// What each key of a node holds, from keys.hpp.
std::optional<KeyType> key_type(std::string_view key) {
  for (const KindDoc& kind : node_kinds) {
    if (kind.key == key) return kind.type;
  }
  for (const KeyDoc& k : common_keys) {
    if (k.key == key) return k.type;
  }
  for (const KindDoc& kind : node_kinds) {
    for (const KeyDoc& k : kind.keys) {
      if (k.key == key) return k.type;
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool is_kind(std::string_view key) {
  return std::ranges::any_of(node_kinds, [&](const KindDoc& k) { return k.key == key; });
}

class Parser {
 public:
  Parser(std::string_view source, std::vector<Token> tokens) : s_(source), t_(std::move(tokens)) {}

  std::optional<Json> screen() {
    Json::Array nodes;
    if (!statements(nodes, false, 0)) return std::nullopt;
    Json out = Json::object();
    out.set("draw", Json(std::move(nodes)));
    return out;
  }

  std::optional<Json> pack() {
    root_ = Json::object();
    while (true) {
      skip_newlines();
      if (peek().type == Tok::End) break;
      if (!pack_statement() || !end_of_statement()) return std::nullopt;
    }
    return std::move(root_);
  }

  [[nodiscard]] const SourceError& error() const { return error_; }

 private:
  // --- Tokens ---

  const Token& peek(std::size_t ahead = 0) const { return t_[std::min(at_ + ahead, t_.size() - 1)]; }
  const Token& next() {
    const Token& t = t_[at_];
    if (at_ + 1 < t_.size()) ++at_;
    return t;
  }
  [[nodiscard]] bool is(Tok type, std::string_view text = {}, std::size_t ahead = 0) const {
    const Token& t = peek(ahead);
    return t.type == type && (text.empty() || t.text == text);
  }
  bool punct(std::string_view p) {
    if (!is(Tok::Punct, p)) return false;
    next();
    return true;
  }

  bool fail_at(const Token& t, std::string what) {
    if (error_.what.empty()) error_ = {t.line, t.column, std::move(what)};
    return false;
  }
  bool fail(std::string what) { return fail_at(peek(), std::move(what)); }
  bool expect(std::string_view p) { return punct(p) || fail("expected \"" + std::string(p) + "\""); }

  void skip_newlines() {
    while (is(Tok::Newline)) next();
  }

  // A deeper line goes on with the statement that began at `indent`.
  void continue_line(std::size_t indent) {
    while (is(Tok::Newline) && peek().indent > indent) next();
  }

  bool end_of_statement() {
    if (is(Tok::Newline) || is(Tok::End) || is(Tok::Punct, "}")) return true;
    return fail("expected the end of the line");
  }

  std::optional<std::string> ident(std::string_view what) {
    if (!is(Tok::Ident)) {
      fail("expected " + std::string(what));
      return std::nullopt;
    }
    return std::string(next().text);
  }

  // --- Values ---

  static std::string unescape(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
      if (raw[i] == '\\' && i + 1 < raw.size()) {
        const char c = raw[++i];
        out += c == 'n' ? '\n' : c == 't' ? '\t' : c;
      } else {
        out += raw[i];
      }
    }
    return out;
  }

  static Json number(std::string_view digits, bool negative) {
    const std::string text = (negative ? "-" : "") + std::string(digits);
    auto json = Json::parse(text);
    return json ? std::move(*json) : Json(text);
  }

  // The tokens of one expression, from at_ on: true when they make one.
  bool expression(int depth = 0) {
    if (depth > max_depth) return fail("the expression is too deep");
    if (!binary(depth)) return false;
    if (punct("?")) return expression(depth + 1) && expect(":") && expression(depth + 1);
    return true;
  }

  bool binary(int depth) {
    if (!unary(depth)) return false;
    static constexpr std::string_view ops[] = {"+", "-", "*", "/", "<", ">", "<=", ">=", "==", "!=", "&&", "||"};
    while (peek().type == Tok::Punct && std::ranges::find(ops, peek().text) != std::end(ops)) {
      next();
      if (!unary(depth)) return false;
    }
    return true;
  }

  bool unary(int depth) {
    while (is(Tok::Punct, "-") || is(Tok::Punct, "!")) next();
    const Token& t = peek();
    switch (t.type) {
      case Tok::Number:
      case Tok::At:
      case Tok::String:
      case Tok::Color:
        next();
        return true;
      case Tok::Ident:
        next();
        if (is(Tok::Punct, "(") && !peek().space_before) {
          next();
          if (punct(")")) return true;
          do {
            if (!expression(depth + 1)) return false;
          } while (punct(","));
          return expect(")");
        }
        if (is(Tok::Punct, ".") && !peek().space_before && is(Tok::Ident, {}, 1)) {
          next();
          next();
        }
        return true;
      case Tok::Punct:
        if (t.text == "(") {
          next();
          return expression(depth + 1) && expect(")");
        }
        break;
      default: break;
    }
    return fail("expected a value");
  }

  // The expression's text as the loader compiles it, or a plain number as a number.
  Json expression_value(std::size_t from, std::size_t to) const {
    if (to - from == 1 && t_[from].type == Tok::Number && t_[from].unit != '%') return number(t_[from].text, false);
    if (to - from == 2 && is_minus(from) && t_[from + 1].type == Tok::Number && t_[from + 1].unit != '%' && !t_[from + 1].space_before) {
      return number(t_[from + 1].text, true);
    }
    std::string text;
    for (std::size_t i = from; i < to; ++i) {
      if (i > from && t_[i].space_before) text += ' ';
      text += s_.substr(t_[i].begin, t_[i].end - t_[i].begin);
    }
    return Json(std::move(text));
  }
  [[nodiscard]] bool is_minus(std::size_t i) const { return t_[i].type == Tok::Punct && t_[i].text == "-"; }

  std::optional<Json> expr_value() {
    const std::size_t from = at_;
    if (!expression()) return std::nullopt;
    return expression_value(from, at_);
  }

  // A plain number, maybe below 0, and nothing more.
  std::optional<Json> plain_number() {
    const bool negative = punct("-");
    if (!is(Tok::Number) || peek().unit == '%') {
      fail("expected a number");
      return std::nullopt;
    }
    return number(next().text, negative);
  }

  // (a, b, ...) of expressions.
  std::optional<std::vector<Json>> tuple() {
    std::vector<Json> out;
    if (!expect("(")) return std::nullopt;
    if (punct(")")) return out;
    do {
      auto value = expr_value();
      if (!value) return std::nullopt;
      out.push_back(std::move(*value));
    } while (punct(","));
    if (!expect(")")) return std::nullopt;
    return out;
  }

  std::optional<Json> text_value() {
    if (is(Tok::String)) return Json(unescape(next().text));
    if (is(Tok::At)) {
      const std::string_view name = next().text;
      if (name.starts_with("tr.")) return Json("#" + std::string(name.substr(3)));
      if (name.find('.') != std::string_view::npos) {
        fail("wording is @tr.key");
        return std::nullopt;
      }
      return Json("@" + std::string(name));
    }
    fail("expected a text: @tr.key, @label or \"words\"");
    return std::nullopt;
  }

  std::optional<Json> value_of(KeyType type, std::string_view key) {
    switch (type) {
      case KeyType::Expr: return expr_value();
      case KeyType::Flag:
        if (is(Tok::Ident, "true") || is(Tok::Ident, "false")) return Json(next().text == "true");
        return Json(true);
      case KeyType::Yes: return Json(true);
      case KeyType::Word:
        if (is(Tok::String)) return Json(unescape(next().text));
        [[fallthrough]];
      case KeyType::Screen:
      case KeyType::Caps:
      case KeyType::Curve:
      case KeyType::Jump: {
        auto word = ident("a name after " + std::string(key));
        if (!word) return std::nullopt;
        return Json(std::move(*word));
      }
      case KeyType::Text: return text_value();
      case KeyType::Exprs: {
        auto items = tuple();
        if (!items) return std::nullopt;
        return Json(Json::Array(std::move(*items)));
      }
      case KeyType::Words: {
        Json::Array out;
        if (!expect("(")) return std::nullopt;
        do {
          auto word = ident("a name");
          if (!word) return std::nullopt;
          out.emplace_back(std::move(*word));
        } while (punct(","));
        if (!expect(")")) return std::nullopt;
        return Json(std::move(out));
      }
      case KeyType::List:
      case KeyType::Args: break;
    }
    fail(std::string(key) + " takes its children in { }");
    return std::nullopt;
  }

  // --- Nodes ---

  // Statements up to the } that closes a block, or the end of the file.
  bool statements(Json::Array& out, bool in_block, int depth) {
    if (depth > max_depth) return fail("too deep");
    while (true) {
      skip_newlines();
      if (is(Tok::End)) return in_block ? fail("a { has no }") : true;
      if (is(Tok::Punct, "}")) return in_block ? (next(), true) : fail("a } with no {");
      auto node = this->node(depth);
      if (!node || !end_of_statement()) return false;
      out.push_back(std::move(*node));
    }
  }

  bool pair(Json& node, std::string_view x, std::string_view y) {
    const Token& at = peek();
    auto items = tuple();
    if (!items) return false;
    if (items->size() != 2) return fail_at(at, "expected (x, y)");
    node.set(x, std::move((*items)[0]));
    node.set(y, std::move((*items)[1]));
    return true;
  }

  std::optional<Json> node(int depth) {
    Json node = Json::object();
    const std::size_t indent = peek().indent;
    std::string kind;
    const auto set_kind = [&](std::string_view k, const Token& at) {
      if (k == "text" && kind == "use") return true;  // what the part reads as @label
      if (k == "use" && kind == "text") {
        kind = "use";
        return true;
      }
      if (!kind.empty()) return fail_at(at, "a node has one kind, and this has " + kind + " already");
      kind = std::string(k);
      return true;
    };
    while (true) {
      continue_line(indent);
      if (is(Tok::Newline) || is(Tok::End) || is(Tok::Punct, "}") || is(Tok::Punct, "{")) break;
      if (is(Tok::Punct, "(")) {
        const Token& at = peek();
        auto items = tuple();
        if (!items) return std::nullopt;
        static constexpr std::string_view box[] = {"x", "y", "w", "h"};
        if (items->size() != 2 && items->size() != 4) {
          fail_at(at, "a box is (x, y) or (x, y, w, h)");
          return std::nullopt;
        }
        for (std::size_t i = 0; i < items->size(); ++i) node.set(box[i], std::move((*items)[i]));
        continue;
      }
      const Token& key_token = peek();
      auto key = ident("a key");
      if (!key) return std::nullopt;
      if (node.find(*key)) {
        fail_at(key_token, "a second " + *key);
        return std::nullopt;
      }
      if (*key == "let" || *key == "set" || *key == "report") {
        if (!set_kind(*key, key_token)) return std::nullopt;
        auto name = ident("a name");
        if (!name || !expect("=")) return std::nullopt;
        node.set(*key, Json(std::move(*name)));
        const std::size_t from = at_;
        if (!expression()) return std::nullopt;
        if (!(*key == "let" && transition(node, from, at_))) node.set("value", expression_value(from, at_));
        continue;
      }
      if (*key == "use") {
        if (!set_kind(*key, key_token)) return std::nullopt;
        auto name = ident("a part's name");
        if (!name) return std::nullopt;
        node.set("use", Json(std::move(*name)));
        if (is(Tok::Punct, "(") && !peek().space_before) {
          auto args = arguments();
          if (!args) return std::nullopt;
          node.set("args", std::move(*args));
        }
        continue;
      }
      if (*key == "about") {
        if (!pair(node, "about_x", "about_y")) return std::nullopt;
        continue;
      }
      if (*key == "to") {
        if (!pair(node, "x2", "y2")) return std::nullopt;
        continue;
      }
      if (*key == "hit_path_at") {
        if (!pair(node, "hit_path_x", "hit_path_y")) return std::nullopt;
        continue;
      }
      if (*key == "group" || *key == "column") {
        if (!set_kind(*key, key_token)) return std::nullopt;
        continue;
      }
      if (*key == "draw" || *key == "args" || *key == "x2" || *key == "y2") {
        fail_at(key_token, *key == "draw" ? "children go in { }" : *key == "args" ? "a part's arguments go in ( ) after its name" : "write to (x2, y2)");
        return std::nullopt;
      }
      const auto type = key_type(*key);
      if (!type) {
        fail_at(key_token, "no key \"" + *key + "\"");
        return std::nullopt;
      }
      if (is_kind(*key) && !set_kind(*key, key_token)) return std::nullopt;
      auto value = value_of(*type, *key);
      if (!value) return std::nullopt;
      node.set(*key, std::move(*value));
    }
    if (is(Tok::Punct, "{")) {
      const char* where = kind.empty() || kind == "group" ? "group" : kind == "column" ? "column" : kind == "repeat" || kind == "use" ? "draw" : nullptr;
      if (!where) {
        fail(kind + " has no children");
        return std::nullopt;
      }
      next();
      Json::Array children;
      if (!statements(children, true, depth + 1)) return std::nullopt;
      node.set(where, Json(std::move(children)));
    } else if (kind.empty() || kind == "group" || kind == "column") {
      node.set(kind.empty() ? "group" : kind, Json::array());
    }
    return node;
  }

  // let x = ease(to, ms): a transition.
  bool transition(Json& node, std::size_t from, std::size_t to) {
    if (to - from < 4 || t_[from].type != Tok::Ident || t_[from].text != "ease" || t_[from + 1].text != "(" || t_[from + 1].space_before) return false;
    std::vector<std::size_t> commas;
    int depth = 0;
    for (std::size_t i = from + 1; i < to; ++i) {
      if (t_[i].type != Tok::Punct) continue;
      if (t_[i].text == "(") ++depth;
      if (t_[i].text == ")" && --depth == 0 && i + 1 != to) return false;  // ease(…) is not all of it
      if (t_[i].text == "," && depth == 1) commas.push_back(i);
    }
    if (commas.size() != 1) return false;
    node.set("to", expression_value(from + 2, commas[0]));
    node.set("ms", expression_value(commas[0] + 1, to - 1));
    return true;
  }

  // (name = value, ...), a part's arguments where it is used or defined.
  std::optional<Json> arguments() {
    Json args = Json::object();
    if (!expect("(")) return std::nullopt;
    if (punct(")")) return args;
    do {
      auto name = ident("an argument's name");
      if (!name || !expect("=")) return std::nullopt;
      auto value = expr_value();
      if (!value) return std::nullopt;
      args.set(*name, std::move(*value));
    } while (punct(","));
    if (!expect(")")) return std::nullopt;
    return args;
  }

  // --- pack.cskin ---

  Json& section(std::string_view key) {
    if (!root_.find(key)) root_.set(key, Json::object());
    return *root_.find(key);
  }

  // ko "…" en "…"
  std::optional<Json> names() {
    Json out = Json::object();
    while (is(Tok::Ident) && is(Tok::String, {}, 1)) {
      const std::string locale(next().text);
      out.set(locale, Json(unescape(next().text)));
    }
    if (!out.as_object() || out.as_object()->empty()) {
      fail("expected a Locale and its words, as ko \"구름\"");
      return std::nullopt;
    }
    return out;
  }

  bool pack_statement() {
    const std::size_t indent = peek().indent;
    const Token& head = peek();
    auto word = ident("a statement");
    if (!word) return false;
    if (*word == "pack") {
      auto id = ident("the pack's id");
      if (!id) return false;
      root_.set("id", Json(std::move(*id)));
      while (continue_line(indent), is(Tok::Ident)) {
        const Token& key = next();
        if (key.text == "format") {
          auto n = plain_number();
          if (!n) return false;
          root_.set("format", std::move(*n));
        } else if (key.text == "extends") {
          auto parent = ident("the id of the pack it extends");
          if (!parent) return false;
          root_.set("extends", Json(std::move(*parent)));
        } else {
          return fail_at(key, "a pack has format and extends");
        }
      }
      return true;
    }
    if (*word == "names") {
      auto n = names();
      if (!n) return false;
      root_.set("names", std::move(*n));
      return true;
    }
    if (*word == "default_theme") {
      auto id = ident("a Theme's id");
      if (!id) return false;
      root_.set("default_theme", Json(std::move(*id)));
      return true;
    }
    if (*word == "theme") {
      auto id = ident("the Theme's id");
      if (!id) return false;
      Json theme = Json::object();
      while (continue_line(indent), is(Tok::Ident)) {
        const Token& key = next();
        if (key.text == "names") {
          auto n = names();
          if (!n) return false;
          theme.set("names", std::move(*n));
        } else if (key.text == "palette" || key.text == "dark" || key.text == "overlay") {
          auto value = ident("a name");
          if (!value) return false;
          theme.set(key.text, Json(std::move(*value)));
        } else {
          return fail_at(key, "a Theme has names, palette, dark and overlay");
        }
      }
      section("themes").set(*id, std::move(theme));
      return true;
    }
    if (*word == "constants") return constants();
    if (*word == "fonts") return fonts();
    if (*word == "style") return style(indent);
    if (*word == "part") return part(indent);
    if (*word == "cursors") return cursors();
    if (*word == "palette") return palette();
    return fail_at(head, "no statement \"" + *word + "\" in pack.cskin");
  }

  bool constants() {
    Json& out = section("constants");
    if (!expect("{")) return false;
    while (true) {
      skip_newlines();
      if (punct("}")) return true;
      auto name = ident("a constant's name");
      if (!name) return false;
      auto value = plain_number();
      if (!value) return false;
      out.set(*name, std::move(*value));
    }
  }

  bool fonts() {
    Json out = Json::object();
    if (!expect("{")) return false;
    while (true) {
      skip_newlines();
      if (punct("}")) break;
      const Token& key = peek();
      auto name = ident("a key of fonts");
      if (!name) return false;
      if (*name == "korean_size" || *name == "other_size") {
        auto value = plain_number();
        if (!value) return false;
        out.set(*name, std::move(*value));
        continue;
      }
      if (*name != "korean" && *name != "other" && *name != "caption") return fail_at(key, "fonts has korean, other, caption, korean_size and other_size");
      Json::Array list;
      while (is(Tok::String)) list.emplace_back(unescape(next().text));
      if (list.empty()) return fail("expected the names of fonts");
      out.set(*name, Json(std::move(list)));
    }
    root_.set("fonts", std::move(out));
    return true;
  }

  bool style(std::size_t indent) {
    auto name = ident("the style's name");
    if (!name) return false;
    Json out = Json::object();
    while (continue_line(indent), is(Tok::Ident)) {
      const Token& key = next();
      const std::string k(key.text);
      if (k == "size" || k == "line_height") {
        auto value = expr_value();
        if (!value) return false;
        out.set(k, std::move(*value));
      } else if (k == "weight" || k == "letter_spacing") {
        auto value = plain_number();
        if (!value) return false;
        out.set(k, std::move(*value));
      } else if (k == "align" || k == "role") {
        auto value = ident("a name after " + k);
        if (!value) return false;
        out.set(k, Json(std::move(*value)));
      } else if (k == "wrap" || k == "ellipsis" || k == "exact" || k == "keep_words") {
        auto value = value_of(KeyType::Flag, k);
        out.set(k, std::move(*value));
      } else {
        return fail_at(key, "no key \"" + k + "\" in a style");
      }
    }
    section("styles").set(*name, std::move(out));
    return true;
  }

  bool part(std::size_t indent) {
    auto name = ident("the part's name");
    if (!name) return false;
    Json out = Json::object();
    if (is(Tok::Punct, "(")) {
      auto args = arguments();
      if (!args) return false;
      out.set("args", std::move(*args));
    }
    while (continue_line(indent), is(Tok::Ident)) {
      const Token& key = next();
      const std::string k(key.text);
      if (k == "w" || k == "h" || k == "height") {
        auto value = expr_value();
        if (!value) return false;
        out.set(k, std::move(*value));
      } else if (k == "snap") {
        out.set(k, std::move(*value_of(KeyType::Flag, k)));
      } else {
        return fail_at(key, "a part has w, h, height and snap");
      }
    }
    Json::Array draw;
    if (!expect("{") || !statements(draw, true, 1)) return false;
    out.set("draw", Json(std::move(draw)));
    section("parts").set(*name, std::move(out));
    return true;
  }

  bool cursors() {
    Json& out = section("cursors");
    if (is(Tok::Ident, "hard")) {
      next();
      out.set("hard", Json(true));
    }
    if (!expect("{")) return false;
    while (true) {
      skip_newlines();
      if (punct("}")) return true;
      auto name = ident("a cursor's name");
      if (!name) return false;
      Json cursor = Json::object();
      while (is(Tok::Ident)) {
        const Token& key = next();
        if (key.text == "part") {
          auto part = ident("a part's name");
          if (!part) return false;
          cursor.set("part", Json(std::move(*part)));
        } else if (key.text == "hot") {
          auto point = tuple();
          if (!point) return false;
          cursor.set("hot", Json(Json::Array(std::move(*point))));
        } else {
          return fail_at(key, "a cursor has part and hot");
        }
      }
      out.set(*name, std::move(cursor));
    }
  }

  bool palette() {
    auto name = ident("the Palette's name");
    if (!name) return false;
    Json out = Json::object();
    if (is(Tok::Ident, "extends")) {
      next();
      auto base = ident("a Palette's name");
      if (!base) return false;
      out.set("extends", Json(std::move(*base)));
    }
    if (!expect("{")) return false;
    while (true) {
      skip_newlines();
      if (punct("}")) break;
      auto entry = ident("an entry's name");
      if (!entry) return false;
      auto value = paint();
      if (!value) return false;
      out.set(*entry, std::move(*value));
    }
    section("palettes").set(*name, std::move(out));
    return true;
  }

  std::optional<Json> paint() {
    if (is(Tok::Color)) return Json(std::string(next().text));
    const Token& head = peek();
    auto fn = ident("a colour, mix, alpha or stops");
    if (!fn || !expect("(")) return std::nullopt;
    if (*fn == "mix") {
      auto a = ident("a colour above it");
      if (!a || !expect(",")) return std::nullopt;
      auto b = ident("a colour above it");
      if (!b || !expect(",")) return std::nullopt;
      if (!is(Tok::Number) || peek().unit != '%') {
        fail("mix takes a percent, as 70%");
        return std::nullopt;
      }
      Json percent = number(next().text, false);
      if (!expect(")")) return std::nullopt;
      Json::Array args;
      args.emplace_back(std::move(*a));
      args.emplace_back(std::move(*b));
      args.push_back(std::move(percent));
      Json out = Json::object();
      out.set("mix", Json(std::move(args)));
      return out;
    }
    if (*fn == "alpha") {
      auto a = ident("a colour above it");
      if (!a || !expect(",")) return std::nullopt;
      auto v = plain_number();
      if (!v || !expect(")")) return std::nullopt;
      Json::Array args;
      args.emplace_back(std::move(*a));
      args.push_back(std::move(*v));
      Json out = Json::object();
      out.set("alpha", Json(std::move(args)));
      return out;
    }
    if (*fn == "stops") {
      Json::Array stops;
      do {
        auto at = plain_number();
        if (!at || !expect(":")) return std::nullopt;
        if (!is(Tok::Color)) {
          fail("expected a colour");
          return std::nullopt;
        }
        Json::Array stop;
        stop.push_back(std::move(*at));
        stop.emplace_back(std::string(next().text));
        stops.emplace_back(std::move(stop));
      } while (punct(","));
      if (!expect(")")) return std::nullopt;
      return Json(std::move(stops));
    }
    fail_at(head, "a Palette entry is a colour, mix, alpha or stops");
    return std::nullopt;
  }

  std::string_view s_;
  std::vector<Token> t_;
  std::size_t at_ = 0;
  Json root_;
  SourceError error_;
};

}  // namespace

std::expected<Json, SourceError> read_cskin(std::string_view text, SourceKind kind) {
  auto tokens = Lexer(text).run();
  if (!tokens) return std::unexpected(tokens.error());
  Parser parser(text, std::move(*tokens));
  auto out = kind == SourceKind::Pack ? parser.pack() : parser.screen();
  if (!out) return std::unexpected(parser.error());
  return std::move(*out);
}

}  // namespace crpp::skin
