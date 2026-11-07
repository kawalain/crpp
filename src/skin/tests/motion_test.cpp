#include "crpp/skin/motion.hpp"

#include "check.hpp"

using namespace crpp::skin;

namespace {

constexpr std::uint64_t key = 7;
constexpr TransitionSpec on{.on = true, .ms_on = 100, .ms_off = 100};
constexpr TransitionSpec off{.on = false, .ms_on = 100, .ms_off = 100};

void test_a_state_on_at_first_sight_runs_up() {
  Transitions t;
  bool moving = false;
  CHECK(t.value(key, on, 1000, true, moving) == 0);
  CHECK(moving);
  const float middle = t.value(key, on, 1050, true, moving);
  CHECK(middle > 0 && middle < 1);
  CHECK(moving);
  CHECK(t.value(key, on, 1100, true, moving) == 1);
  CHECK(!moving);
}

void test_first_jumps_starts_at_the_end_and_runs_only_on_a_change() {
  TransitionSpec jump = on;
  jump.first_jumps = true;
  Transitions t;
  bool moving = true;
  CHECK(t.value(key, jump, 1000, true, moving) == 1);
  CHECK(!moving);
  jump.on = false;
  CHECK(t.value(key, jump, 1010, true, moving) == 1);
  CHECK(moving);
  CHECK(t.value(key, jump, 1110, true, moving) == 0);
  CHECK(!moving);
  CHECK(t.size() == 1);  // it must still tell the next change from a first sight
}

void test_off_and_settled_keeps_nothing() {
  Transitions t;
  bool moving = false;
  CHECK(t.value(key, off, 1000, true, moving) == 0);
  CHECK(!moving);
  CHECK(t.size() == 0);
  (void)t.value(key, on, 1000, true, moving);
  (void)t.value(key, on, 1100, true, moving);
  (void)t.value(key, off, 1100, true, moving);
  (void)t.value(key, off, 1200, true, moving);
  CHECK(t.size() == 0);
}

void test_turning_around_starts_from_where_it_is_and_takes_less() {
  TransitionSpec line = on;
  line.curve = Curve::Linear;
  Transitions t;
  bool moving = false;
  (void)t.value(key, line, 1000, true, moving);
  CHECK(t.value(key, line, 1040, true, moving) == 0.4f);
  line.on = false;
  CHECK(t.value(key, line, 1040, true, moving) == 0.4f);
  CHECK(moving);
  CHECK(t.value(key, line, 1080, true, moving) == 0);
  CHECK(!moving);
}

void test_the_duration_off_can_differ() {
  TransitionSpec spec{.on = true, .ms_on = 100, .ms_off = 400, .first_jumps = true, .curve = Curve::Linear};
  Transitions t;
  bool moving = false;
  (void)t.value(key, spec, 1000, true, moving);
  spec.on = false;
  (void)t.value(key, spec, 1000, true, moving);
  CHECK(t.value(key, spec, 1100, true, moving) == 0.75f);
}

void test_what_a_frame_did_not_ask_for_is_gone() {
  Transitions t;
  bool moving = false;
  (void)t.value(key, on, 1000, true, moving);
  t.next_frame();
  (void)t.value(key, on, 1100, true, moving);
  t.next_frame();
  CHECK(t.size() == 1);
  t.next_frame();
  CHECK(t.size() == 0);
  CHECK(t.value(key, on, 2000, true, moving) == 0);  // it runs again when it comes back
  CHECK(moving);
}

void test_without_animation_it_is_at_its_end() {
  Transitions t;
  bool moving = true;
  CHECK(t.value(key, on, 1000, false, moving) == 1);
  CHECK(!moving);
  CHECK(t.value(key, off, 1000, false, moving) == 0);
  CHECK(t.size() == 0);
}

void test_the_curves() {
  CHECK(curve_at(Curve::Ease, 0) == 0);
  CHECK(curve_at(Curve::Ease, 1) == 1);
  CHECK(curve_at(Curve::Ease, 0.5f) > 0.75f);
  CHECK(curve_at(Curve::Linear, 0.5f) == 0.5f);
}

}  // namespace

int main() {
  test_a_state_on_at_first_sight_runs_up();
  test_first_jumps_starts_at_the_end_and_runs_only_on_a_change();
  test_off_and_settled_keeps_nothing();
  test_turning_around_starts_from_where_it_is_and_takes_less();
  test_the_duration_off_can_differ();
  test_what_a_frame_did_not_ask_for_is_gone();
  test_without_animation_it_is_at_its_end();
  test_the_curves();
  return crpp_test_result();
}
