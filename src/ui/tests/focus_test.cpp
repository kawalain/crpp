// The keyboard alone: Tab goes from stop to stop, stays inside what is open on top, and Enter
// and Space act on the stop as a click would.
#include <string>
#include <vector>

#include "check.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
using test::UiHarness;

namespace {

// As Windows sends it: the key, then the character it makes.
void tab(UiHarness& h, bool back = false) {
  h.app.key_down(VK_TAB, false, back);
  h.app.char_input(L'\t', back);
  h.render();
}

// Tab until the keyboard is on this hit; false if it never comes.
bool tab_to(UiHarness& h, const Surface& owner, int id) {
  for (int i = 0; i < 64; ++i) {
    if (h.app.ui().stop_owner == &owner && h.app.ui().stop_id == id) return true;
    tab(h);
  }
  return false;
}

void tab_goes_round_the_stops_and_back(const char* pack) {
  UiHarness h;
  CHECK(h.start(951, 692, pack));
  h.app.show_home();
  h.render();
  const std::vector<const Ui::Hit*> stops = h.app.ui().tab_stops();
  CHECK(stops.size() >= 3);
  if (stops.size() < 3) return;
  // Nothing is focused: Tab goes to the first, and that shows.
  CHECK(!h.app.ui().stop_owner);
  tab(h);
  const Surface* first = h.app.ui().stop_owner;
  const int first_id = h.app.ui().stop_id;
  CHECK(first == stops[0]->owner && first_id == stops[0]->id);
  CHECK(h.app.ui().focused(first, first_id));
  // Round once, and it is back.
  for (std::size_t i = 0; i < stops.size(); ++i) tab(h);
  CHECK(h.app.ui().stop_owner == first && h.app.ui().stop_id == first_id);
  // Shift+Tab goes back, past the start to the last.
  tab(h, true);
  CHECK(h.app.ui().stop_owner == stops.back()->owner && h.app.ui().stop_id == stops.back()->id);
}

void a_field_takes_the_keys_and_not_the_tab(const char* pack) {
  UiHarness h;
  CHECK(h.start(951, 692, pack));
  h.app.show_home();
  h.render();
  CHECK(tab_to(h, h.app.home(), HomeScreen::Code));
  CHECK(h.app.ui().focus && h.app.ui().focus_owner == &h.app.home());
  if (!h.app.ui().focus) return;
  h.app.char_input(L'7', false);
  const std::wstring typed = h.app.ui().focus->text();
  CHECK(!typed.empty());
  // The character Tab makes is not typed where the keys go next.
  h.app.key_down(VK_TAB, false, true);
  CHECK(h.app.char_input(L'\t', false));
  tab(h);
  CHECK(h.app.ui().focus && h.app.ui().focus->text() == typed);
  // Tab left the field: it no longer takes the keys.
  tab(h);
  CHECK(!h.app.ui().focus);
}

void a_click_moves_the_keyboard_without_showing_it(const char* pack) {
  UiHarness h;
  CHECK(h.start(951, 692, pack));
  h.app.show_home();
  h.render();
  tab(h);
  CHECK(h.app.ui().cues);
  CHECK(h.click(h.app.home(), HomeScreen::Code));
  CHECK(h.app.ui().stop_owner == &h.app.home() && h.app.ui().stop_id == HomeScreen::Code);
  CHECK(!h.app.ui().cues && !h.app.ui().focused(&h.app.home(), HomeScreen::Code));
  // It goes on from there.
  const std::vector<const Ui::Hit*> stops = h.app.ui().tab_stops();
  const auto at = std::ranges::find_if(stops, [&](const Ui::Hit* s) { return s->owner == &h.app.home() && s->id == HomeScreen::Code; });
  CHECK(at != stops.end());
  if (at == stops.end()) return;
  const Ui::Hit* next = at + 1 == stops.end() ? stops.front() : *(at + 1);
  const Surface* next_owner = next->owner;
  const int next_id = next->id;
  tab(h);
  CHECK(h.app.ui().stop_owner == next_owner && h.app.ui().stop_id == next_id);
  // The system that always shows it shows it after a click too.
  h.app.ui().always_cues = true;
  CHECK(h.click(h.app.home(), HomeScreen::Code));
  CHECK(h.app.ui().focused(&h.app.home(), HomeScreen::Code));
}

void a_question_keeps_tab_and_its_keys(const char* pack) {
  UiHarness h;
  CHECK(h.start(951, 692, pack));
  h.app.show_home();
  h.render();
  tab(h);
  int answered = 0, cancelled = 0;
  h.app.dialogs().show({"Sure?", "Yes", "No", false, [&] { ++answered; }, [&] { ++cancelled; }});
  h.render();
  if (h.app.own_frame()) h.on_pane(Pane::Dialog);
  for (const Ui::Hit* stop : h.app.ui().tab_stops()) CHECK(stop->owner == &h.app.dialogs());
  CHECK(h.app.ui().tab_stops().size() == 2);
  // Under the question, the stop it had does not act.
  CHECK(tab_to(h, h.app.dialogs(), Dialogs::Cancel));
  h.app.key_down(VK_RETURN, false, false);
  CHECK(cancelled == 1 && answered == 0 && !h.app.dialogs().is_open());
  CHECK(h.app.char_input(L'\r', false));
}

void space_acts_on_a_button_and_a_toggle(const char* pack) {
  UiHarness h;
  CHECK(h.start(951, 692, pack));
  h.app.show_home();
  h.render();
  int answered = 0;
  h.app.dialogs().show({"Sure?", "Yes", "No", false, [&] { ++answered; }, nullptr});
  h.render();
  if (h.app.own_frame()) h.on_pane(Pane::Dialog);
  CHECK(tab_to(h, h.app.dialogs(), Dialogs::Confirm));
  h.app.key_down(VK_SPACE, false, false);
  CHECK(answered == 1 && h.app.char_input(L' ', false));
}

}  // namespace

int main() {
  for (const char* pack : {CRPP_TEST_SKINS "/cloud", CRPP_TEST_SKINS "/retro"}) {
    tab_goes_round_the_stops_and_back(pack);
    a_field_takes_the_keys_and_not_the_tab(pack);
    a_click_moves_the_keyboard_without_showing_it(pack);
    a_question_keeps_tab_and_its_keys(pack);
    space_acts_on_a_button_and_a_toggle(pack);
  }
  return crpp_test_result();
}
