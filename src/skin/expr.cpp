#include "crpp/skin/expr.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace crpp::skin {

namespace {

constexpr int max_nesting = 16;

[[nodiscard]] bool is_name_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
[[nodiscard]] bool is_digit(char c) { return c >= '0' && c <= '9'; }

class Compiler {
 public:
  Compiler(std::string_view source, Names& names, std::vector<Instr>& code) : s_(source), names_(names), code_(code) {}

  bool run() {
    if (!ternary()) return false;
    skip_space();
    if (at_ < s_.size()) return fail("something is left after the expression");
    return true;
  }

  [[nodiscard]] const std::string& error() const { return error_; }

 private:
  bool fail(std::string what) {
    if (error_.empty()) error_ = std::move(what) + " in \"" + std::string(s_) + "\"";
    return false;
  }

  void skip_space() {
    while (at_ < s_.size() && (s_[at_] == ' ' || s_[at_] == '\t')) ++at_;
  }

  bool eat(std::string_view token) {
    skip_space();
    if (s_.substr(at_, token.size()) != token) return false;
    at_ += token.size();
    return true;
  }

  bool emit(Op op, int stack_change, std::uint16_t arg = 0, float value = 0) {
    code_.push_back({op, arg, value});
    stack_ += stack_change;
    if (stack_ > max_expr_stack) return fail("the expression is too deep");
    return true;
  }

  bool ternary() {
    if (nesting_ >= max_nesting) return fail("the expression is too deep");
    ++nesting_;
    bool ok = or_();
    if (ok && eat("?")) {
      ok = ternary() && (eat(":") || fail("a ? has no :")) && ternary() && emit(Op::Select, -2);
    }
    --nesting_;
    return ok;
  }

  bool or_() {
    if (!and_()) return false;
    while (eat("||")) {
      if (!and_() || !emit(Op::Or, -1)) return false;
    }
    return true;
  }

  bool and_() {
    if (!equality()) return false;
    while (eat("&&")) {
      if (!equality() || !emit(Op::And, -1)) return false;
    }
    return true;
  }

  bool equality() {
    if (!comparison()) return false;
    for (;;) {
      if (eat("==")) {
        if (!comparison() || !emit(Op::Equal, -1)) return false;
      } else if (eat("!=")) {
        if (!comparison() || !emit(Op::NotEqual, -1)) return false;
      } else {
        return true;
      }
    }
  }

  bool comparison() {
    if (!sum()) return false;
    for (;;) {
      if (eat("<=")) {
        if (!sum() || !emit(Op::LessEq, -1)) return false;
      } else if (eat(">=")) {
        if (!sum() || !emit(Op::GreaterEq, -1)) return false;
      } else if (eat("<")) {
        if (!sum() || !emit(Op::Less, -1)) return false;
      } else if (eat(">")) {
        if (!sum() || !emit(Op::Greater, -1)) return false;
      } else {
        return true;
      }
    }
  }

  bool sum() {
    if (!product()) return false;
    for (;;) {
      if (eat("+")) {
        if (!product() || !emit(Op::Add, -1)) return false;
      } else if (eat("-")) {
        if (!product() || !emit(Op::Sub, -1)) return false;
      } else {
        return true;
      }
    }
  }

  bool product() {
    if (!unary()) return false;
    for (;;) {
      if (eat("*")) {
        if (!unary() || !emit(Op::Mul, -1)) return false;
      } else if (eat("/")) {
        if (!unary() || !emit(Op::Div, -1)) return false;
      } else {
        return true;
      }
    }
  }

  bool unary() {
    if (eat("-")) {
      if (!unary()) return false;
      if (!code_.empty() && code_.back().op == Op::Const && last_is_literal_) {  // -1 stays one number
        code_.back().value = -code_.back().value;
        return true;
      }
      return emit(Op::Neg, 0);
    }
    if (eat("!")) return unary() && emit(Op::Not, 0);
    return primary();
  }

  std::string_view name_at() {
    const std::size_t from = at_;
    while (at_ < s_.size() && (is_name_start(s_[at_]) || is_digit(s_[at_]))) ++at_;
    return s_.substr(from, at_ - from);
  }

  bool primary() {
    skip_space();
    last_is_literal_ = false;
    if (at_ >= s_.size()) return fail("the expression ends too early");
    const char c = s_[at_];
    if (c == '(') {
      ++at_;
      return ternary() && (eat(")") || fail("a ( has no )"));
    }
    if (is_digit(c) || c == '.') return number();
    if (c == '$') {
      ++at_;
      const std::string_view name = name_at();
      const auto paint = names_.paint(name);
      if (!paint) return fail("no Palette entry \"" + std::string(name) + "\"");
      if (!emit(Op::Const, 1, 0, static_cast<float>(*paint))) return false;
      return true;
    }
    if (c == '@') {
      ++at_;
      const std::string_view name = name_at();
      const auto bound = names_.number(name);
      if (!bound) return fail("this screen has no number \"" + std::string(name) + "\"");
      return emit(Op::Number, 1, *bound);
    }
    if (is_name_start(c)) return name();
    return fail(std::string("unexpected \"") + c + "\"");
  }

  bool number() {
    float value = 0;
    const char* const first = s_.data() + at_;
    const auto [end, ec] = std::from_chars(first, s_.data() + s_.size(), value);
    if (ec != std::errc{}) return fail("a bad number");
    at_ += static_cast<std::size_t>(end - first);
    if (!emit(Op::Const, 1, 0, value)) return false;
    last_is_literal_ = true;
    return true;
  }

  bool name() {
    const std::string_view word = name_at();
    skip_space();
    if (at_ < s_.size() && s_[at_] == '(') return call(word);
    if (at_ < s_.size() && s_[at_] == '.') {
      ++at_;
      return member(word, name_at());
    }
    if (const auto var = var_named(word)) return emit(Op::Var, 1, *var);
    if (const auto slot = names_.slot(word)) return emit(Op::Slot, 1, *slot);
    if (const auto constant = names_.constant(word)) return emit(Op::Const, 1, 0, *constant);
    return fail("no name \"" + std::string(word) + "\"");
  }

  bool member(std::string_view owner, std::string_view field) {
    const auto box = names_.box(owner);
    if (!box) return fail("no sibling with the id \"" + std::string(owner) + "\"");
    const std::uint16_t x = *box, y = static_cast<std::uint16_t>(x + 1), w = static_cast<std::uint16_t>(x + 2), h = static_cast<std::uint16_t>(x + 3);
    if (field == "x") return emit(Op::Slot, 1, x);
    if (field == "y") return emit(Op::Slot, 1, y);
    if (field == "w") return emit(Op::Slot, 1, w);
    if (field == "h") return emit(Op::Slot, 1, h);
    if (field == "right") return emit(Op::Slot, 1, x) && emit(Op::Slot, 1, w) && emit(Op::Add, -1);
    if (field == "bottom") return emit(Op::Slot, 1, y) && emit(Op::Slot, 1, h) && emit(Op::Add, -1);
    return fail("a box has no \"" + std::string(field) + "\"");
  }

  // 'text in quotes', for a text or a style's name.
  std::optional<std::string_view> quoted() {
    skip_space();
    if (at_ >= s_.size() || s_[at_] != '\'') return std::nullopt;
    const std::size_t from = ++at_;
    const std::size_t end = s_.find('\'', from);
    if (end == std::string_view::npos) return std::nullopt;
    at_ = end + 1;
    return s_.substr(from, end - from);
  }

  bool call(std::string_view word) {
    ++at_;  // the (
    const auto one = [&](Op op) { return ternary() && (eat(")") || fail("a ( has no )")) && emit(op, 0); };
    const auto two = [&](Op op) {
      return ternary() && (eat(",") || fail(std::string(word) + " takes two")) && ternary() && (eat(")") || fail("a ( has no )")) && emit(op, -1);
    };
    if (word == "floor") return one(Op::Floor);
    if (word == "ceil") return one(Op::Ceil);
    if (word == "round") return one(Op::Round);
    if (word == "snap") return one(Op::Snap);
    if (word == "min") return two(Op::Min);
    if (word == "max") return two(Op::Max);
    if (word == "hit" || word == "note") {
      const auto named = quoted();
      if (!named || !eat(")")) return fail(std::string(word) + " takes 'name'");
      const auto id = word == "hit" ? names_.hit(*named) : names_.note(*named);
      if (!id) return fail("this screen has no " + std::string(word) + " \"" + std::string(*named) + "\"");
      return emit(Op::Const, 1, 0, static_cast<float>(*id));
    }
    if (word == "textw") {
      const auto style = quoted();
      if (!style || !eat(",")) return fail("textw takes 'style', 'text' and maybe a width");
      const auto text = quoted();
      if (!text) return fail("textw takes 'style', 'text' and maybe a width");
      const auto style_id = names_.style(*style);
      if (!style_id) return fail("no style \"" + std::string(*style) + "\"");
      const auto text_id = names_.text(*text);
      if (!text_id) return fail("no such text \"" + std::string(*text) + "\"");
      if (eat(",")) return ternary() && (eat(")") || fail("a ( has no )")) && emit(Op::TextWidthIn, 0, *style_id, static_cast<float>(*text_id));
      if (!eat(")")) return fail("textw takes 'style', 'text' and maybe a width");
      return emit(Op::TextWidth, 1, *style_id, static_cast<float>(*text_id));
    }
    if (word == "texth") {
      const auto style = quoted();
      if (!style || !eat(",")) return fail("texth takes 'style', 'text', width");
      const auto text = quoted();
      if (!text || !eat(",")) return fail("texth takes 'style', 'text', width");
      const auto style_id = names_.style(*style);
      if (!style_id) return fail("no style \"" + std::string(*style) + "\"");
      const auto text_id = names_.text(*text);
      if (!text_id) return fail("no such text \"" + std::string(*text) + "\"");
      return ternary() && (eat(")") || fail("a ( has no )")) && emit(Op::TextHeight, 0, *style_id, static_cast<float>(*text_id));
    }
    return fail("no function \"" + std::string(word) + "\"");
  }

  std::string_view s_;
  Names& names_;
  std::vector<Instr>& code_;
  std::string error_;
  std::size_t at_ = 0;
  int stack_ = 0, nesting_ = 0;
  bool last_is_literal_ = false;
};

}  // namespace

std::optional<Var> var_named(std::string_view name) {
  static constexpr std::array<std::string_view, VarCount> names{"W", "H", "X", "Y", "x", "y", "w", "h", "u", "font_size", "hot", "pressed", "i", "flow"};
  for (std::size_t i = 0; i < names.size(); ++i) {
    if (names[i] == name) return static_cast<Var>(i);
  }
  return std::nullopt;
}

std::expected<Expr, std::string> compile(std::string_view source, Names& names, std::vector<Instr>& code) {
  if (source.size() > max_expr_chars) return std::unexpected("an expression longer than " + std::to_string(max_expr_chars) + " characters");
  const std::size_t start = code.size();
  Compiler compiler(source, names, code);
  if (!compiler.run()) {
    code.resize(start);
    return std::unexpected(compiler.error());
  }
  if (code.size() == start + 1 && code[start].op == Op::Const) {
    const float value = code[start].value;
    code.resize(start);
    return Expr::constant(value);
  }
  return Expr::range(static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(code.size() - start));
}

float eval(std::span<const Instr> code, Expr expr, Env& env) {
  if (expr.is_constant()) return expr.value();
  std::array<float, max_expr_stack + 1> stack;
  int top = -1;
  const Instr* const end = code.data() + expr.start() + expr.count();
  for (const Instr* i = code.data() + expr.start(); i != end; ++i) {
    switch (i->op) {
      case Op::Const: stack[++top] = i->value; break;
      case Op::Slot: stack[++top] = env.frame[i->arg]; break;
      case Op::Var: stack[++top] = env.vars[i->arg]; break;
      case Op::Number: stack[++top] = env.number(i->arg); break;
      case Op::TextWidth: stack[++top] = env.text_width(i->arg, static_cast<std::uint32_t>(i->value), Env::unlimited_text_width); break;
      case Op::TextWidthIn: stack[top] = env.text_width(i->arg, static_cast<std::uint32_t>(i->value), stack[top]); break;
      case Op::TextHeight: stack[top] = env.text_height(i->arg, static_cast<std::uint32_t>(i->value), stack[top]); break;
      case Op::Snap: stack[top] = env.snap(stack[top]); break;
      case Op::Add: --top; stack[top] += stack[top + 1]; break;
      case Op::Sub: --top; stack[top] -= stack[top + 1]; break;
      case Op::Mul: --top; stack[top] *= stack[top + 1]; break;
      case Op::Div: --top; stack[top] = stack[top + 1] != 0 ? stack[top] / stack[top + 1] : 0; break;
      case Op::Less: --top; stack[top] = stack[top] < stack[top + 1] ? 1.0f : 0.0f; break;
      case Op::LessEq: --top; stack[top] = stack[top] <= stack[top + 1] ? 1.0f : 0.0f; break;
      case Op::Greater: --top; stack[top] = stack[top] > stack[top + 1] ? 1.0f : 0.0f; break;
      case Op::GreaterEq: --top; stack[top] = stack[top] >= stack[top + 1] ? 1.0f : 0.0f; break;
      case Op::Equal: --top; stack[top] = stack[top] == stack[top + 1] ? 1.0f : 0.0f; break;
      case Op::NotEqual: --top; stack[top] = stack[top] != stack[top + 1] ? 1.0f : 0.0f; break;
      case Op::And: --top; stack[top] = stack[top] != 0 && stack[top + 1] != 0 ? 1.0f : 0.0f; break;
      case Op::Or: --top; stack[top] = stack[top] != 0 || stack[top + 1] != 0 ? 1.0f : 0.0f; break;
      case Op::Not: stack[top] = stack[top] == 0 ? 1.0f : 0.0f; break;
      case Op::Neg: stack[top] = -stack[top]; break;
      case Op::Select: top -= 2; stack[top] = stack[top] != 0 ? stack[top + 1] : stack[top + 2]; break;
      case Op::Min: --top; stack[top] = std::min(stack[top], stack[top + 1]); break;
      case Op::Max: --top; stack[top] = std::max(stack[top], stack[top + 1]); break;
      case Op::Floor: stack[top] = std::floor(stack[top]); break;
      case Op::Ceil: stack[top] = std::ceil(stack[top]); break;
      case Op::Round: stack[top] = std::round(stack[top]); break;
    }
  }
  return stack[top];
}

}  // namespace crpp::skin
