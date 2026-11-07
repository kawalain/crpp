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
    if (name == "gap") return 0;
    return std::nullopt;
  }
  std::optional<std::uint16_t> box(std::string_view name) override {
    if (name == "field") return 1;
    return std::nullopt;
  }
  std::optional<float> constant(std::string_view name) override {
    if (name == "ten") return 10.0f;
    return std::nullopt;
  }
  std::optional<std::uint16_t> number(std::string_view name) override {
    if (name == "seats") return 7;
    return std::nullopt;
  }
  std::optional<std::uint16_t> paint(std::string_view name) override {
    if (name == "ink") return 3;
    if (name == "face") return 4;
    return std::nullopt;
  }
  std::optional<std::uint16_t> style(std::string_view name) override {
    if (name == "body") return 2;
    return std::nullopt;
  }
  std::optional<std::uint32_t> text(std::string_view source) override {
    if (source == "#ok") return 9;
    return std::nullopt;
  }
};

class TestEnv final : public Env {
 public:
  float number(std::uint16_t id) override { return id == 7 ? 5.0f : -1.0f; }
  float text_width(std::uint16_t style, std::uint32_t text, float width) override { return static_cast<float>(style) * 100 + static_cast<float>(text) + (width == unlimited_text_width ? 0 : width); }
  float text_height(std::uint16_t style, std::uint32_t text, float width) override { return static_cast<float>(style) * 100 + static_cast<float>(text) + width; }
  float snap(float value) override { return std::round(value * 2) / 2; }
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
  CHECK(run("min(3,4)") == 3);
  CHECK(run("max(3,4)") == 4);
  CHECK(run("floor(1.9)") == 1);
  CHECK(run("ceil(1.1)") == 2);
  CHECK(run("round(1.5)") == 2);
  CHECK(run("max(75,ceil(20.2)+24)") == 75);
  CHECK(run("textw('body','#ok')") == 209);
  CHECK(run("textw('body','#ok',50)") == 259);
  CHECK(run("texth('body','#ok',W)") == 409);
  CHECK(run("1+texth('body','#ok',1+1)") == 212);
  CHECK(run("snap(1.3)") == 1.5f);
}

void names_read_what_they_mean() {
  CHECK(run("W") == 200);
  CHECK(run("W-2*H") == 0);
  CHECK(run("hot ? 1 : 2") == 1);
  CHECK(run("pressed ? 1 : 2") == 2);
  CHECK(run("gap") == 6);
  CHECK(run("ten+1") == 11);
  CHECK(run("field.x") == 10);
  CHECK(run("field.y") == 20);
  CHECK(run("field.w") == 30);
  CHECK(run("field.h") == 40);
  CHECK(run("field.right") == 40);
  CHECK(run("field.bottom") == 60);
  CHECK(run("@seats") == 5);
  CHECK(run("$face") == 4);
  CHECK(run("hot ? $ink : $face") == 3);
}

void a_lone_number_needs_no_program() {
  TestNames names;
  std::vector<Instr> code;
  const auto plain = compile("12", names, code);
  CHECK(plain && plain->is_constant() && plain->value() == 12);
  const auto negative = compile("-12", names, code);
  CHECK(negative && negative->is_constant() && negative->value() == -12);
  const auto paint = compile("$ink", names, code);
  CHECK(paint && paint->is_constant() && paint->value() == 3);
  CHECK(code.empty());
}

void bad_expressions_are_refused() {
  for (const std::string_view source : {"", "1+", "(1", "1)", "1 2", "nobody", "$nobody", "@nobody", "nobody.x", "field.nothing", "nothing(1)", "min(1)", "min(1,2", "1?2", "textw('nobody','#ok')",
                                        "textw('body','#nothing')", "textw('body')", "textw('body','#ok',", "texth('body','#ok')", "texth('nobody','#ok',1)", "snap()", "#", "1=2", "'text'"}) {
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
  a_lone_number_needs_no_program();
  bad_expressions_are_refused();
  sizes_are_limited();
  return crpp_test_result();
}
