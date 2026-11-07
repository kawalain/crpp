// Home drawn from the Messenger pack in skins/: the same places to click as the C++ layout, and
// the same results. Their pixels are compared with tests/shots while both forms exist.
#include <string>
#include <vector>

#include "check.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
using test::UiHarness;

namespace {

constexpr const char* messenger_pack = CRPP_TEST_SKINS "/messenger";

struct Place {
  int id;
  Rect rect;
};

std::vector<Place> places(UiHarness& h) {
  const HomeScreen& home = h.app.home();
  std::vector<Place> found;
  const int ids[] = {HomeScreen::Create, HomeScreen::Join, HomeScreen::Code, HomeScreen::Find, HomeScreen::SeatBase, HomeScreen::SeatBase + 1};
  for (const int id : ids) {
    if (const Ui::Hit* hit = h.app.ui().find_hit(&home, id)) found.push_back({id, hit->rect});
  }
  return found;
}

void hold_two_seats(UiHarness& h) {
  h.app.home().held_seats = [] { return std::vector<std::string>{"ABC234", "XYZ789"}; };
  h.app.home().held_seat_ms = []() -> std::optional<std::uint64_t> { return 60000; };
}

void start_on_home(UiHarness& h) {
  CHECK(h.start(300, 560));
  h.app.set_theme("messenger");
  h.app.show_home();
  h.app.home().set_nickname("Me");
  hold_two_seats(h);
}

void test_the_pack_puts_everything_where_the_code_does() {
  UiHarness h;
  start_on_home(h);
  h.render();
  const std::vector<Place> code = places(h);
  CHECK(code.size() == 6);
  CHECK(h.app.load_skin_dir(messenger_pack).has_value());
  CHECK(h.app.ui().pack_screen("home") != nullptr);
  h.render();
  const std::vector<Place> pack = places(h);
  CHECK(pack.size() == code.size());
  for (std::size_t i = 0; i < std::min(pack.size(), code.size()); ++i) {
    CHECK(pack[i].id == code[i].id);
    CHECK(pack[i].rect.x == code[i].rect.x && pack[i].rect.y == code[i].rect.y);
    CHECK(pack[i].rect.w == code[i].rect.w && pack[i].rect.h == code[i].rect.h);
  }
}

void test_clicks_on_the_pack_do_what_they_do_on_the_code() {
  UiHarness h;
  start_on_home(h);
  CHECK(h.app.load_skin_dir(messenger_pack).has_value());
  HomeScreen& home = h.app.home();
  int created = 0, found = 0;
  std::string joined;
  home.on_create = [&] { ++created; };
  home.on_find_public = [&] { ++found; };
  home.on_join = [&](std::string code) { joined = std::move(code); };
  h.render();

  CHECK(h.click(home, HomeScreen::Create));
  CHECK(created == 1);
  CHECK(h.click(home, HomeScreen::SeatBase + 1));
  CHECK(joined == "XYZ789");

  joined.clear();
  CHECK(h.click(home, HomeScreen::Code));
  CHECK(h.app.ui().focus != nullptr);
  for (const wchar_t c : std::wstring(L"abc234")) h.app.char_input(c, false);
  h.render();
  CHECK(h.click(home, HomeScreen::Join));
  CHECK(joined == "ABC234");

  // With no Seat held, the button goes straight to a public Room here too.
  home.held_seat_ms = []() -> std::optional<std::uint64_t> { return std::nullopt; };
  home.held_seats = nullptr;
  h.render();
  CHECK(!h.drawn(home, HomeScreen::SeatBase));
  CHECK(h.click(home, HomeScreen::Find));
  CHECK(found == 1);
}

void test_a_click_in_the_field_lands_in_the_text() {
  UiHarness h;
  start_on_home(h);
  CHECK(h.app.load_skin_dir(messenger_pack).has_value());
  HomeScreen& home = h.app.home();
  h.render();
  CHECK(h.click(home, HomeScreen::Code));
  for (const wchar_t c : std::wstring(L"ABC234")) h.app.char_input(c, false);
  h.render();
  const Rect caret_after_typing = h.app.ui().caret;
  CHECK(caret_after_typing.h > 0);
  // Far left of the field: the caret goes back to the start, by the style the pack drew with.
  const Ui::Hit* field = h.app.ui().find_hit(&home, HomeScreen::Code);
  CHECK(field != nullptr);
  if (!field) return;
  const float x = field->rect.x + 1, y = field->rect.y + field->rect.h / 2;
  h.app.mouse_move(x, y);
  h.app.mouse_down(x, y, false);
  h.app.mouse_up();
  h.render();
  CHECK(h.app.ui().caret.x < caret_after_typing.x);
}

void test_a_theme_outside_the_pack_is_drawn_as_before() {
  UiHarness h;
  start_on_home(h);
  CHECK(h.app.load_skin_dir(messenger_pack).has_value());
  CHECK(h.app.ui().pack_screen("home") != nullptr);
  h.app.set_theme("messengerdark");
  CHECK(h.app.ui().pack_screen("home") != nullptr);
  h.app.set_theme("cloud");
  CHECK(h.app.ui().pack_screen("home") == nullptr);
  CHECK(h.render());
  h.app.set_theme("messenger");
  CHECK(h.app.ui().pack_screen("home") != nullptr);
  CHECK(h.render());
}

void test_graphics_can_be_let_go_with_a_pack_drawn() {
  UiHarness h;
  start_on_home(h);
  CHECK(h.app.load_skin_dir(messenger_pack).has_value());
  CHECK(h.render());
  h.drop_target();
  h.app.release_graphics();
  CHECK(h.make_target());
  CHECK(h.render());
}

void test_the_prompt_from_the_pack_takes_typing_and_clicks() {
  UiHarness h;
  CHECK(h.start(420, 400));
  h.app.set_theme("messenger");
  CHECK(h.app.load_skin_dir(messenger_pack).has_value());
  CHECK(h.app.ui().pack_screen("prompt") != nullptr);
  PromptScreen& prompt = h.app.prompt();
  std::string submitted;
  int linked = 0;
  prompt.on_submit = [&](std::string value) { submitted = std::move(value); };
  prompt.on_link = [&] { ++linked; };
  h.app.show_prompt({.placeholder = "password", .link = "forgot", .password = true, .valid = [](std::string_view v) { return !v.empty(); }});
  h.render();

  // Empty, the button is drawn but does nothing.
  CHECK(h.click(prompt, PromptScreen::Submit));
  CHECK(submitted.empty());
  CHECK(h.click(prompt, PromptScreen::Field));
  for (const wchar_t c : std::wstring(L"abc")) h.app.char_input(c, false);
  h.render();
  CHECK(!prompt.password_shown());
  CHECK(h.click(prompt, PromptScreen::Eye));
  CHECK(prompt.password_shown());
  CHECK(h.click(prompt, PromptScreen::Link));
  CHECK(linked == 1);
  CHECK(h.click(prompt, PromptScreen::Submit));
  CHECK(submitted == "abc");
  CHECK(!h.drawn(prompt, PromptScreen::Back));
}

void test_a_folder_without_a_pack_is_refused_and_changes_nothing() {
  UiHarness h;
  start_on_home(h);
  const auto loaded = h.app.load_skin_dir(CRPP_TEST_SKINS "/no-such-pack");
  CHECK(!loaded.has_value());
  CHECK(h.app.ui().pack == nullptr);
  CHECK(h.render());
}

}  // namespace

int main() {
  test_the_pack_puts_everything_where_the_code_does();
  test_clicks_on_the_pack_do_what_they_do_on_the_code();
  test_a_click_in_the_field_lands_in_the_text();
  test_a_theme_outside_the_pack_is_drawn_as_before();
  test_graphics_can_be_let_go_with_a_pack_drawn();
  test_the_prompt_from_the_pack_takes_typing_and_clicks();
  test_a_folder_without_a_pack_is_refused_and_changes_nothing();
  return crpp_test_result();
}
