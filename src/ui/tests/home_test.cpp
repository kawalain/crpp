#include <string>

#include "check.hpp"
#include "crpp/ui/home_logic.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
using test::UiHarness;

namespace {

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
  CHECK(calls == 0);
}

}  // namespace

int main() {
  test_code_is_trimmed_and_upper_case();
  test_entry_for_code();
  test_join_on_enter_and_on_the_arrow();
  test_short_code_does_not_join();
  test_search_finds_a_public_room();
  test_busy_buttons_do_nothing();
  return crpp_test_result();
}
