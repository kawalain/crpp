#include <string>

#include "check.hpp"
#include "crpp/ui/home_logic.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
using test::UiHarness;

namespace {

void test_public_rooms_are_read_from_health() {
  CHECK(home::public_rooms_in_health("ok\nrooms=12\npublic_rooms=3") == 3);
  CHECK(home::public_rooms_in_health("ok\r\nrooms=0\r\npublic_rooms=0\r\n") == 0);
  CHECK(home::public_rooms_in_health("ok") == std::nullopt);
  CHECK(home::public_rooms_in_health("ok\npublic_rooms=") == std::nullopt);
  CHECK(home::public_rooms_in_health("ok\npublic_rooms=-1") == std::nullopt);
  CHECK(home::public_rooms_in_health("ok\npublic_rooms=9999999999") == std::nullopt);
  CHECK(home::public_rooms_in_health("") == std::nullopt);
}

// "계절과 날씨 효과": off, Home has no season, a Room counts nothing and stays clear, and the display tab turns it back on.
void test_sky_effects_turn_off_the_season_and_the_weather() {
  UiHarness h;
  CHECK(h.start());
  Prefs& prefs = *h.app.ui().prefs;
  CHECK(prefs.get(pref::sky_effects));
  h.app.home().set_date(12, 24);
  CHECK(h.app.home().season() == home::Season::Winter);

  h.app.settings().open(SettingsPage::Display);
  h.render();
  DisplayTab& display = h.app.settings().display();
  CHECK(h.click(display, DisplayTab::SkyEffects));
  CHECK(!prefs.get(pref::sky_effects));
  CHECK(h.app.home().season() == home::Season::None);
  h.app.settings().close();

  h.app.show_session("ABC234");
  RoomScreen& room = h.app.room();
  room.set_room({{0, "a", "", false, "c0"}, {1, "b", "", false, "c1"}}, 0, 0, false, false);
  room.note_input(10000);
  CHECK(room.weather().step() == RoomWeather::Clear);
  CHECK(room.weather().wake_at() == 0);
  CHECK(room.shown_weather() == RoomWeather::Clear);

  prefs.set(pref::sky_effects, true);
  CHECK(h.app.home().season() == home::Season::Winter);
}

void test_code_is_trimmed_and_upper_case() {
  CHECK(home::normalize_code(L"abc234") == L"ABC234");
  CHECK(home::normalize_code(L"ABC234") == L"ABC234");
  CHECK(home::normalize_code(L" ab234 ") == std::nullopt);        // five once trimmed
  CHECK(home::normalize_code(L"\x3000" L"abc234") == L"ABC234");  // an IME's full-width space
  CHECK(home::normalize_code(L"") == std::nullopt);
  CHECK(home::normalize_code(L"ABC23") == std::nullopt);
  CHECK(home::normalize_code(L"ABC2345") == std::nullopt);
  CHECK(home::normalize_code(L"구름구름구름") == L"구름구름구름");  // the server's to refuse
}

void test_entry_for_code() {
  using Kind = home::Entry::Kind;
  auto join = home::entry_for_code("ABC234", std::nullopt);
  CHECK(join.kind == Kind::Join && join.code == "ABC234" && join.key.empty());
  auto resume = home::entry_for_code("ABC234", "KEY");
  CHECK(resume.kind == Kind::Resume && resume.code == "ABC234" && resume.key == "KEY");
}

void type_code(UiHarness& h, const wchar_t* text) {
  CHECK(h.click(h.app.home(), HomeScreen::Code));
  for (const wchar_t* c = text; *c; ++c) h.app.char_input(*c, false);
  h.render();
}

void test_join_on_enter_and_on_the_arrow() {
  UiHarness h;
  CHECK(h.start());
  std::string joined;
  h.app.home().on_join = [&](std::string code) { joined = std::move(code); };
  type_code(h, L"abc234");
  h.app.char_input(L'\r', false);
  CHECK(joined == "ABC234");
  joined.clear();
  CHECK(h.click(h.app.home(), HomeScreen::Join));
  CHECK(joined == "ABC234");
}

void test_short_code_does_not_join() {
  UiHarness h;
  CHECK(h.start());
  int joined = 0;
  h.app.home().on_join = [&](std::string) { ++joined; };
  type_code(h, L"abc");
  h.app.char_input(L'\r', false);
  CHECK(joined == 0);
  CHECK(h.click(h.app.home(), HomeScreen::Join));
  CHECK(joined == 0);
}

void test_search_finds_a_public_room() {
  UiHarness h;
  CHECK(h.start());
  int found = 0;
  h.app.home().on_find_public = [&] { ++found; };
  CHECK(h.click(h.app.home(), HomeScreen::Find));
  CHECK(found == 1);
}

// Coming back to Home asks the server again how many public Rooms are open; staying on it does not.
void test_some_days_have_a_greeting_of_their_own() {
  CHECK(home::special_day(1, 1) == home::SpecialDay::NewYear);
  CHECK(home::special_day(4, 1) == home::SpecialDay::AprilFools);
  CHECK(home::special_day(11, 7) == home::SpecialDay::Birthday);
  CHECK(home::special_day(12, 24) == home::SpecialDay::Christmas);
  CHECK(home::special_day(12, 25) == home::SpecialDay::Christmas);
  CHECK(home::special_day(12, 26) == home::SpecialDay::None);
  CHECK(home::special_day(0, 0) == home::SpecialDay::None);
}

void test_seasons_follow_the_month() {
  CHECK(home::season(12) == home::Season::Winter);
  CHECK(home::season(2) == home::Season::Winter);
  CHECK(home::season(3) == home::Season::Spring);
  CHECK(home::season(5) == home::Season::Spring);
  CHECK(home::season(7) == home::Season::None);
  CHECK(home::season(9) == home::Season::Autumn);
  CHECK(home::season(11) == home::Season::Autumn);
  CHECK(home::season(0) == home::Season::None);  // no date, as in a shot
}

void test_a_code_can_hold_words() {
  CHECK(home::words_in_code("ABC234") == 0);
  CHECK(home::words_in_code("ZZZ7KQ") == home::WordZzz);
  CHECK(home::words_in_code("7KZZZZ") == home::WordZzz);
  CHECK(home::words_in_code("CAKE42") == home::WordCake);
  CHECK(home::words_in_code("X9STAR") == home::WordStar);
  CHECK(home::words_in_code("ZZZSTA") == home::WordZzz);  // a word cut short is none
  // The server's alphabet has no I or O, so no word may need them.
  for (const std::string_view word : home::code_words) CHECK(word.find_first_of("IO01") == std::string_view::npos);
}

void test_coming_home_asks_once() {
  UiHarness h;
  CHECK(h.start());
  int shown = 0;
  h.app.home().on_shown = [&] { ++shown; };
  h.app.show_home();
  CHECK(shown == 0);
  h.app.show_session("ABC234");
  h.app.show_home();
  CHECK(shown == 1);
  h.app.show_home();
  CHECK(shown == 1);
}

void test_busy_buttons_do_nothing() {
  UiHarness h;
  CHECK(h.start());
  HomeScreen& home = h.app.home();
  int calls = 0;
  home.busy = [] { return true; };
  home.on_create = [&] { ++calls; };
  home.on_join = [&](std::string) { ++calls; };
  home.on_find_public = [&] { ++calls; };
  type_code(h, L"abc234");
  CHECK(h.click(home, HomeScreen::Join));
  CHECK(h.click(home, HomeScreen::Find));
  h.app.mouse_down(180, 260, false);  // the create cloud
  h.app.mouse_up(180, 260);
  CHECK(calls == 0);
}

void test_return_decisions() {
  using Kind = home::Entry::Kind;
  CHECK(home::find_step(true) == home::FindStep::AskReturn);
  CHECK(home::find_step(false) == home::FindStep::Random);
  CHECK(home::entry_for_return(std::nullopt).kind == Kind::Random);  // expired meanwhile
  const auto back = home::entry_for_return(home::HeldSeat{"ABC234", "KEY"});
  CHECK(back.kind == Kind::Resume && back.code == "ABC234" && back.key == "KEY");
}

struct ReturnRig {
  UiHarness h;
  int found = 0, returned = 0;
  std::optional<std::uint64_t> seat = 60000;
  bool start() {
    if (!h.start()) return false;
    HomeScreen& home = h.app.home();
    home.on_find_public = [this] { ++found; };
    home.on_return = [this] { ++returned; };
    home.held_seat_ms = [this] { return seat; };
    h.render();
    return true;
  }
  bool ask() { return h.click(h.app.home(), HomeScreen::Find); }
};

void test_held_seat_asks_first() {
  ReturnRig r;
  CHECK(r.start());
  HomeScreen& home = r.h.app.home();
  CHECK(home.returning());
  CHECK(r.ask());
  CHECK(home.asking_return());
  CHECK(r.found == 0 && r.returned == 0);
  CHECK(r.h.drawn(home, HomeScreen::ReturnConfirm));
  CHECK(r.h.click(home, HomeScreen::ReturnConfirm));
  CHECK(!home.asking_return());
  CHECK(r.returned == 1 && r.found == 0);
  CHECK(!r.h.drawn(home, HomeScreen::ReturnConfirm));
}

void test_second_choice_joins_at_random() {
  ReturnRig r;
  CHECK(r.start());
  CHECK(r.ask());
  CHECK(r.h.click(r.h.app.home(), HomeScreen::ReturnRandom));
  CHECK(r.found == 1 && r.returned == 0);
}

void test_escape_and_backdrop_only_cancel() {
  ReturnRig r;
  CHECK(r.start());
  HomeScreen& home = r.h.app.home();
  CHECK(r.ask());
  r.h.key(VK_ESCAPE);
  CHECK(!home.asking_return());
  CHECK(r.ask());
  r.h.app.mouse_down(5, 5, false);
  r.h.app.mouse_up(5, 5);
  r.h.render();
  CHECK(!home.asking_return());
  CHECK(r.found == 0 && r.returned == 0);
}

void test_question_covers_the_droplet_and_the_card_keeps_it_open() {
  ReturnRig r;
  CHECK(r.start());
  HomeScreen& home = r.h.app.home();
  CHECK(r.ask());
  const Ui::Hit* drop = r.h.app.ui().find_hit(&r.h.app.settings(), SettingsPanel::Droplet);
  CHECK(drop != nullptr);
  r.h.app.mouse_down(drop->rect.x + 5, drop->rect.y + 20, false);
  r.h.app.mouse_up(drop->rect.x + 5, drop->rect.y + 20);
  r.h.render();
  CHECK(!r.h.app.settings().is_open());
  CHECK(!home.asking_return());
  CHECK(r.ask());
  CHECK(r.h.click(home, HomeScreen::ReturnCard));
  CHECK(home.asking_return());
}

void test_question_is_shown_only_on_home() {
  ReturnRig r;
  CHECK(r.start());
  HomeScreen& home = r.h.app.home();
  CHECK(r.ask());
  r.h.app.show_session("ABC234");  // an earlier attempt got through
  r.h.render();
  CHECK(!r.h.drawn(home, HomeScreen::ReturnConfirm));
  r.h.app.show_home();
  r.h.render();
  CHECK(!home.asking_return());
  CHECK(!r.h.drawn(home, HomeScreen::ReturnConfirm));
}

void test_no_question_while_busy() {
  ReturnRig r;
  CHECK(r.start());
  HomeScreen& home = r.h.app.home();
  home.busy = [] { return true; };
  CHECK(r.ask());
  CHECK(!home.asking_return());
  home.busy = nullptr;
  CHECK(r.ask());
  home.busy = [] { return true; };
  CHECK(r.h.click(home, HomeScreen::ReturnConfirm));
  CHECK(!home.asking_return() && r.returned == 0);
}

void test_seat_gone_goes_straight_to_random() {
  ReturnRig r;
  CHECK(r.start());
  r.seat.reset();
  CHECK(r.ask());
  CHECK(!r.h.app.home().asking_return());
  CHECK(r.found == 1);
}

void test_seat_expiry_needs_no_paint_and_no_spinning() {
  ReturnRig r;
  CHECK(r.start());
  HomeScreen& home = r.h.app.home();
  const auto at = home.wake_at();
  CHECK(at != 0);
  r.seat.reset();
  CHECK(!home.tick(at - 1));
  CHECK(home.returning());
  CHECK(home.tick(at));  // no render in between, as in a hidden window
  CHECK(!home.returning());
  CHECK(home.wake_at() == 0);
  CHECK(!home.tick(at + 1));
}

// Waits out every transition the way the frame timer does.
void settle(UiHarness& h) {
  for (int i = 0; i < 60 && h.app.animating(); ++i) {
    Sleep(16);
    (void)h.app.tick(clock_ms());
    h.render();
  }
  CHECK(!h.app.animating());
  (void)h.app.tick(clock_ms());  // the last frame's
  CHECK(!h.app.tick(clock_ms()));
}

void test_hover_animates_then_goes_idle() {
  UiHarness h;
  CHECK(h.start());
  h.app.ui().animations = true;
  HomeScreen& home = h.app.home();
  CHECK(!h.app.animating());
  const Ui::Hit* find = h.app.ui().find_hit(&home, HomeScreen::Find);
  CHECK(find != nullptr);
  CHECK(h.app.mouse_move(find->rect.x + 10, find->rect.y + 10));  // true: the window is drawn again
  h.render();  // a paint starts the transition
  CHECK(h.app.animating());
  settle(h);
  CHECK(h.app.mouse_leave());
  h.render();
  CHECK(h.app.animating());
  settle(h);
  CHECK(h.app.wake_at() == 0);
}

void test_focus_and_droplet_animate_then_go_idle() {
  UiHarness h;
  CHECK(h.start());
  h.app.ui().animations = true;
  CHECK(h.click(h.app.home(), HomeScreen::Code));
  settle(h);
  h.app.mouse_down(5, 5, false);  // focus leaves
  h.app.mouse_up(5, 5);
  h.render();
  CHECK(h.app.animating());
  settle(h);
  const Ui::Hit* drop = h.app.ui().find_hit(&h.app.settings(), SettingsPanel::Droplet);
  CHECK(drop != nullptr);
  CHECK(h.app.mouse_move(drop->rect.x + 17, drop->rect.y + 30));
  h.render();  // a paint starts the droplet's transition
  CHECK(h.app.animating());
  settle(h);
}

void test_bubble_mutes_the_search_label_until_it_is_gone() {
  UiHarness h;
  CHECK(h.start());
  HomeScreen& home = h.app.home();
  CHECK(h.click(home, HomeScreen::Find));
  const auto before = clock_ms();
  home.show_error("지금은 들어갈 수 있는 공개 구름이 없어요.");
  const auto wake = home.wake_at();
  CHECK(wake >= before + Toasts::default_ms + 200);
  CHECK(home.tick(wake));
  CHECK(home.wake_at() == 0);
}

}  // namespace

int main() {
  test_sky_effects_turn_off_the_season_and_the_weather();
  test_hover_animates_then_goes_idle();
  test_focus_and_droplet_animate_then_go_idle();
  test_bubble_mutes_the_search_label_until_it_is_gone();
  test_return_decisions();
  test_held_seat_asks_first();
  test_second_choice_joins_at_random();
  test_escape_and_backdrop_only_cancel();
  test_question_covers_the_droplet_and_the_card_keeps_it_open();
  test_question_is_shown_only_on_home();
  test_no_question_while_busy();
  test_seat_gone_goes_straight_to_random();
  test_seat_expiry_needs_no_paint_and_no_spinning();
  test_code_is_trimmed_and_upper_case();
  test_public_rooms_are_read_from_health();
  test_entry_for_code();
  test_join_on_enter_and_on_the_arrow();
  test_short_code_does_not_join();
  test_search_finds_a_public_room();
  test_coming_home_asks_once();
  test_a_code_can_hold_words();
  test_seasons_follow_the_month();
  test_some_days_have_a_greeting_of_their_own();
  test_busy_buttons_do_nothing();
  return crpp_test_result();
}
