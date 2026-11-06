#include "crpp/sound/idle.hpp"

#include "check.hpp"

using crpp::sound::IdleWatch;

namespace {

constexpr std::uint32_t limit = 60000;

void waits_exactly_until_the_limit_can_be_reached() {
  IdleWatch watch;
  const auto step = watch.check(10000, 9000, limit);
  CHECK(!step.idle);
  CHECK(step.check_after_ms == limit - 1000);
  CHECK(!watch.idle());
}

void becomes_idle_and_stops_asking_for_checks() {
  IdleWatch watch;
  const auto step = watch.check(100000, 100000 - limit, limit);
  CHECK(step.idle);
  CHECK(step.check_after_ms == 0);
  CHECK(watch.idle());
}

void input_before_the_due_time_only_moves_the_next_check() {
  IdleWatch watch;
  (void)watch.check(0, 0, limit);
  const auto step = watch.check(limit, limit - 5000, limit);
  CHECK(!step.idle);
  CHECK(step.check_after_ms == limit - 5000);
}

void input_ends_being_away_even_before_the_os_reports_it() {
  IdleWatch watch;
  (void)watch.check(limit, 0, limit);
  CHECK(watch.idle());
  const auto step = watch.input(limit + 10, limit);
  CHECK(!step.idle);
  CHECK(step.check_after_ms == limit);
  CHECK(!watch.idle());
  // The OS still has the old time, as raw input can run ahead of it.
  CHECK(!watch.check(limit + 20, 0, limit).idle);
}

void a_last_input_ahead_of_now_counts_as_just_now() {
  IdleWatch watch;
  const auto step = watch.check(1000, 1500, limit);
  CHECK(!step.idle);
  CHECK(step.check_after_ms == limit);
}

void ticks_may_wrap() {
  IdleWatch watch;
  const std::uint32_t before_wrap = 0xFFFFFFF0u;
  const auto step = watch.check(0x10, before_wrap, limit);
  CHECK(!step.idle);
  CHECK(step.check_after_ms == limit - 0x20);
}

void reset_forgets_everything() {
  IdleWatch watch;
  (void)watch.input(5, limit);
  (void)watch.check(5 + limit, 0, limit);
  CHECK(watch.idle());
  watch.reset();
  CHECK(!watch.idle());
  CHECK(watch.check(limit, 0, limit).idle);
}

}  // namespace

int main() {
  waits_exactly_until_the_limit_can_be_reached();
  becomes_idle_and_stops_asking_for_checks();
  input_before_the_due_time_only_moves_the_next_check();
  input_ends_being_away_even_before_the_os_reports_it();
  a_last_input_ahead_of_now_counts_as_just_now();
  ticks_may_wrap();
  reset_forgets_everything();
  return crpp_test_result();
}
