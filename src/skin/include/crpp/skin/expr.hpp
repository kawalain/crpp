#pragma once
// The arithmetic in a Skin Pack: numbers, names, + - * /, comparisons, && || !, a ? b : c, and
// min, max, floor, ceil, round, textw. It is compiled once, when the pack loads, into a short
// program in one array that all of a pack's expressions share; running one allocates nothing.
// There are no assignments, loops or calls of anything else: a pack holds no code.
#include <array>
#include <bit>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace crpp::skin {

enum class Op : std::uint8_t {
  Const, Slot, Var, Number,
  Add, Sub, Mul, Div,
  Less, LessEq, Greater, GreaterEq, Equal, NotEqual,
  And, Or, Not, Neg, Select,
  Min, Max, Floor, Ceil, Round, TextWidth,
};

struct Instr {
  Op op = Op::Const;
  std::uint16_t arg = 0;
  float value = 0;
};

// A constant, or a range of the shared program.
class Expr {
 public:
  constexpr Expr() = default;
  [[nodiscard]] static Expr constant(float value) {
    Expr e;
    e.start_ = std::bit_cast<std::uint32_t>(value);
    return e;
  }
  [[nodiscard]] static constexpr Expr range(std::uint32_t start, std::uint32_t count) {
    Expr e;
    e.start_ = start;
    e.count_ = count;
    return e;
  }
  [[nodiscard]] bool is_constant() const { return count_ == 0; }
  [[nodiscard]] float value() const { return std::bit_cast<float>(start_); }
  [[nodiscard]] std::uint32_t start() const { return start_; }
  [[nodiscard]] std::uint32_t count() const { return count_; }

 private:
  std::uint32_t start_ = 0, count_ = 0;
};

// What every expression can read without its pack naming it.
//   W H    the size of the box this node is in
//   X Y    where that box is on the window
//   x y w h  this node's own box, as far as it has been worked out
//   u      one line's thickness, a whole number of pixels
//   font_size  the body text's size in this language
//   hot pressed  the pointer is over, or the button went down on, the nearest hit around this
//   i      which round of the nearest repeat this is
enum Var : std::uint8_t { VarW, VarH, VarX, VarY, Varx, Vary, Varw, Varh, VarU, VarFontSize, VarHot, VarPressed, VarI, VarCount };

[[nodiscard]] std::optional<Var> var_named(std::string_view name);

// What the other names mean where an expression is being compiled.
class Names {
 public:
  virtual ~Names() = default;
  // A let, or a part's argument.
  [[nodiscard]] virtual std::optional<std::uint16_t> slot(std::string_view name) = 0;
  // A sibling with an id: the slot of its x, followed by y, w and h.
  [[nodiscard]] virtual std::optional<std::uint16_t> box(std::string_view name) = 0;
  // @name, a number the screen supplies.
  [[nodiscard]] virtual std::optional<std::uint16_t> number(std::string_view name) = 0;
  // $name, a Palette entry.
  [[nodiscard]] virtual std::optional<std::uint16_t> paint(std::string_view name) = 0;
  [[nodiscard]] virtual std::optional<std::uint16_t> style(std::string_view name) = 0;
  // A text as a node's "text" takes it. Null when it is no good.
  [[nodiscard]] virtual std::optional<std::uint32_t> text(std::string_view source) = 0;
};

inline constexpr std::size_t max_expr_chars = 400;
inline constexpr int max_expr_stack = 32;

// Adds to `code`. The error says what is wrong, in words for whoever writes packs.
[[nodiscard]] std::expected<Expr, std::string> compile(std::string_view source, Names& names, std::vector<Instr>& code);

// What a running expression reads.
class Env {
 public:
  virtual ~Env() = default;
  std::array<float, VarCount> vars{};
  const float* frame = nullptr;  // the slots of the screen or part being walked
  [[nodiscard]] virtual float number(std::uint16_t /*id*/) { return 0; }
  [[nodiscard]] virtual float text_width(std::uint16_t /*style*/, std::uint32_t /*text*/) { return 0; }
};

[[nodiscard]] float eval(std::span<const Instr> code, Expr expr, Env& env);

}  // namespace crpp::skin
