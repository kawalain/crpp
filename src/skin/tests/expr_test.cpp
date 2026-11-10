#include "crpp/skin/expr.hpp"

#include <cmath>
#include <map>
#include <string>

#include "check.hpp"

using namespace crpp::skin;

namespace {

class TestNames final : public Names {
 public:
  std::optional<std::uint16_t> slot(std::string_view name) override {
    if (name == "gap") return std::uint16_t{0};
    return std::nullopt;
  }
  std::optional<std::uint16_t> box(std::string_view name) override {
    if (name == "field") return std::uint16_t{1};
    return std::nullopt;
  }
  std::optional<float> constant(std::string_view name) override {
    if (name == "ten") return 10.0f;
    if (name == "dozen") return 12.0f;
    return std::nullopt;
  }
  std::optional<std::uint16_t> number(std::string_view name) override {
    if (name == "seats") return std::uint16_t{7};
    return std::nullopt;
  }
  std::optional<std::uint16_t> paint(std::string_view name) override {
    if (name == "ink") return std::uint16_t{3};
    if (name == "face") return std::uint16_t{4};
    if (name == "ten") return std::uint16_t{5};  // also a constant, which the two cannot share
    return std::nullopt;
  }
  std::optional<std::uint16_t> style(std::string_view name) override {
    if (name == "body") return std::uint16_t{2};
    return std::nullopt;
  }
  std::optional<std::uint32_t> text(std::string_view source) override {
    if (source == "#ok") return 9;
    if (source == "@label") return 8;
    if (source == "words") return 7;
    return std::nullopt;
  }
  std::optional<int> hit(std::string_view name) override {
    if (name == "close") return 12;
    return std::nullopt;
  }
};

class TestEnv final : public Env {
 public:
  float number(std::uint16_t id) override { return id == 7 ? 5.0f : -1.0f; }
  float text_width(std::uint16_t style, std::uint32_t text, float width) override { return static_cast<float>(style) * 100 + static_cast<float>(text) + (width == unlimited_text_width ? 0 : width); }
  float text_height(std::uint16_t style, std::uint32_t text, float width) override { return static_cast<float>(style) * 100 + static_cast<float>(text) + width; }
  float snap(float value) override { return std::round(value * 2) / 2; }
  float mix_colors(float, float, float percent) override { return percent; }
};

float run(std::string_view source) {
  TestNames names;
  TestEnv env;
  const float frame[] = {6, 10, 20, 30, 40};  // gap, then field's x y w h
  env.frame = frame;
  env.vars[VarW] = 200;
  env.vars[VarH] = 100;
  env.vars[VarHot] = 1;
  std::vector<Instr> code;
  const auto compiled = compile(source, names, code);
  CHECK(compiled.has_value());
  return compiled ? eval(code, *compiled, env) : NAN;
}

bool refused(std::string_view source) {
  TestNames names;
  std::vector<Instr> code{{Op::Const, 0, 1}};
  const auto compiled = compile(source, names, code);
  CHECK(code.size() == 1);  // a refused expression leaves nothing behind
  return !compiled;
}

void arithmetic_follows_precedence() {
  CHECK(run("1+2*3") == 7);
  CHECK(run("(1+2)*3") == 9);
  CHECK(run("10-4-3") == 3);
  CHECK(run("-2*3") == -6);
  CHECK(run("2--3") == 5);
  CHECK(run("-(1+2)") == -3);
  CHECK(run("1.5+.5") == 2);
  CHECK(run(" 1 +  2 ") == 3);
}

void a_division_by_zero_is_zero() {
  CHECK(run("1/0") == 0);
  CHECK(run("7/2") == 3.5f);
}

void comparisons_and_logic() {
  CHECK(run("1<2") == 1);
  CHECK(run("2<=2") == 1);
  CHECK(run("2>2") == 0);
  CHECK(run("2>=2") == 1);
  CHECK(run("2==2") == 1);
  CHECK(run("2!=2") == 0);
  CHECK(run("1&&0") == 0);
  CHECK(run("1||0") == 1);
  CHECK(run("!0") == 1);
  CHECK(run("!3") == 0);
  CHECK(run("1<2 && 2<3") == 1);
}

void a_choice_nests_to_the_right() {
  CHECK(run("1?2:3") == 2);
  CHECK(run("0?2:3") == 3);
  CHECK(run("0?1:0?2:3") == 3);
  CHECK(run("1?0?7:8:9") == 8);
}

void functions() {
  CHECK(run("ease(0)") == 0);
  CHECK(run("ease(1)") == 1);
  CHECK(run("ease(0.5)") > 0.5f);
  CHECK(run("bezier(0, 0, 1, 1, 2)") == 1);
  CHECK(std::abs(run("bezier(0, 0, 1, 1, 0.5)") - 0.5f) < 0.001f);
  CHECK(run("bezier(0.3, 1.7, 0.5, 1, 0.5)") > 1);  // it may overshoot
  CHECK(run("min(3,4)") == 3);
  CHECK(run("max(3,4)") == 4);
  CHECK(run("floor(1.9)") == 1);
  CHECK(run("ceil(1.1)") == 2);
  CHECK(run("round(1.5)") == 2);
  CHECK(run("max(75,ceil(20.2)+24)") == 75);
  CHECK(run("snap(1.3)") == 1.5f);
  CHECK(refused("min (3, 4)"));  // a call's ( comes right after its name
}

void names_read_what_they_mean() {
  CHECK(run("@W") == 200);
  CHECK(run("@W-2*@H") == 0);
  CHECK(run("@hot ? 1 : 2") == 1);
  CHECK(run("@pressed ? 1 : 2") == 2);
  CHECK(run("@seats") == 5);
  CHECK(run("gap") == 6);
  CHECK(run("dozen+1") == 13);
  CHECK(run("field.x") == 10);
  CHECK(run("field.y") == 20);
  CHECK(run("field.w") == 30);
  CHECK(run("field.h") == 40);
  CHECK(run("field.right") == 40);
  CHECK(run("field.bottom") == 60);
  CHECK(run("ink") == 3);  // a Palette entry is bare
  CHECK(run("@hot ? ink : face") == 3);
  CHECK(refused("W"));  // the exe's own name without its @
  CHECK(refused("$ink"));
  CHECK(refused("ten"));  // a constant and a Palette entry of one name
  CHECK(refused("@tr.ok"));
}

void units_and_fractions() {
  CHECK(run("40%") == 0.4f);
  CHECK(run("150ms") == 150);
  CHECK(run("150ms+1") == 151);
  CHECK(run("mix(ink, face, 40%)") == 40);  // the percent itself, as exact as before
  CHECK(run("mix(ink, face, -40%)") == -40);
  CHECK(run("mix(ink, face, @hot)") == 100);
  CHECK(run("mix(ink, face, 0.25)") == 25);
  CHECK(refused("150mss"));
}

void calls_take_bare_names_and_texts() {
  CHECK(run("textw(body, @tr.ok)") == 209);
  CHECK(run("textw(body, @tr.ok, 50)") == 259);
  CHECK(run("textw(body, @label, 50)") == 258);
  CHECK(run("texth(body, @tr.ok, @W)") == 409);
  CHECK(run("1+texth(body, @tr.ok, 1+1)") == 212);
  CHECK(run("texth(body, \"words\", @W)") == 407);
  CHECK(run("hit(close)") == 12);
  CHECK(refused("textw('body', '#ok')"));
}

void a_lone_number_needs_no_program() {
  TestNames names;
  std::vector<Instr> code;
  const auto plain = compile("12", names, code);
  CHECK(plain && plain->is_constant() && plain->value() == 12);
  const auto negative = compile("-12", names, code);
  CHECK(negative && negative->is_constant() && negative->value() == -12);
  const auto paint = compile("ink", names, code);
  CHECK(paint && paint->is_constant() && paint->value() == 3);
  CHECK(code.empty());
}

void bad_expressions_are_refused() {
  for (const std::string_view source : {"", "1+", "(1", "1)", "1 2", "nobody", "@nobody", "nobody.x", "field.nothing", "nothing(1)", "min(1)", "min(1,2", "ease(1,2)", "bezier(1,2,3,4)", "bezier(1,2,3,4,5,6)", "1?2", "textw(nobody, @tr.ok)",
                                        "textw(body, @tr.nothing)", "textw(body)", "textw(body, @tr.ok,", "texth(body, @tr.ok)", "texth(nobody, @tr.ok, 1)", "snap()", "#", "1=2", "'text'"}) {
    CHECK(refused(source));
  }
}

void sizes_are_limited() {
  CHECK(refused(std::string(max_expr_chars + 1, '1')));
  // Too many values waiting at once: 1+(1+(1+...
  std::string deep;
  for (int i = 0; i < 40; ++i) deep += "1+(";
  deep += "1";
  deep += std::string(40, ')');
  CHECK(refused(deep));
  // As deep as it may go still runs inside the stack it was promised.
  std::string fits;
  for (int i = 0; i < 12; ++i) fits += "1+(";
  fits += "1";
  fits += std::string(12, ')');
  CHECK(run(fits) == 13);
}

}  // namespace

int main() {
  arithmetic_follows_precedence();
  a_division_by_zero_is_zero();
  comparisons_and_logic();
  a_choice_nests_to_the_right();
  functions();
  names_read_what_they_mean();
  units_and_fractions();
  calls_take_bare_names_and_texts();
  a_lone_number_needs_no_program();
  bad_expressions_are_refused();
  sizes_are_limited();
  return crpp_test_result();
}
