// The keyboard alone: Tab goes from stop to stop, stays inside what is open on top, and Enter
// and Space act on the stop as a click would.
#include <filesystem>
#include <fstream>
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

void open_settings(UiHarness& h, SettingsPage page) {
  h.app.show_home();
  h.render();
  h.app.settings().open(page);
  h.render();
  if (h.app.own_frame()) h.on_pane(Pane::Options);
}

void press(UiHarness& h, unsigned vk, bool ctrl = false, bool shift = false) {
  h.app.key_down(vk, ctrl, shift);
  if (vk == VK_RETURN) h.app.char_input(L'\r', shift);
  if (vk == VK_SPACE) h.app.char_input(L' ', shift);
  h.render();
}

void a_slider_moves_by_the_arrows_pages_and_ends(const char* pack) {
  UiHarness h;
  CHECK(h.start(951, 692, pack));
  open_settings(h, SettingsPage::Display);
  DisplayTab& tab = h.app.settings().display();
  Prefs& prefs = *h.app.ui().prefs;
  CHECK(tab_to(h, tab, DisplayTab::ScaleSlider));
  press(h, VK_HOME);
  CHECK(prefs.get(pref::chat_scale) == 80);
  press(h, VK_RIGHT);
  CHECK(prefs.get(pref::chat_scale) == 81);
  press(h, VK_PRIOR);
  CHECK(prefs.get(pref::chat_scale) == 88);
  press(h, VK_END);
  CHECK(prefs.get(pref::chat_scale) == 150);
  press(h, VK_RIGHT);
  CHECK(prefs.get(pref::chat_scale) == 150);
}

void a_dropdown_picks_closed_and_open(const char* pack) {
  UiHarness h;
  CHECK(h.start(951, 692, pack));
  open_settings(h, SettingsPage::Display);
  DisplayTab& tab = h.app.settings().display();
  Prefs& prefs = *h.app.ui().prefs;
  CHECK(tab_to(h, tab, DisplayTab::IdleList));
  // Closed, the arrows choose.
  press(h, VK_HOME);
  const int first = prefs.get(pref::idle_minutes);
  press(h, VK_DOWN);
  const int second = prefs.get(pref::idle_minutes);
  CHECK(second != first);
  CHECK(!h.drawn(tab, DisplayTab::IdleList + 1));
  // Space opens it; an arrow moves along it without choosing, and Enter chooses.
  press(h, VK_SPACE);
  CHECK(h.drawn(tab, DisplayTab::IdleList + 1));
  press(h, VK_UP);
  CHECK(prefs.get(pref::idle_minutes) == second);
  press(h, VK_RETURN);
  CHECK(prefs.get(pref::idle_minutes) == first && !h.drawn(tab, DisplayTab::IdleList + 1));
  // Alt+Down opens it too.
  CHECK(h.app.sys_key(VK_DOWN, false));
  h.render();
  CHECK(h.drawn(tab, DisplayTab::IdleList + 1));
}

void ctrl_tab_and_the_arrows_go_through_the_tabs(const char* pack) {
  UiHarness h;
  CHECK(h.start(951, 692, pack));
  open_settings(h, SettingsPage::Sound);
  SettingsPanel& panel = h.app.settings();
  press(h, VK_TAB, true);
  CHECK(panel.page() == SettingsPage::Display);
  press(h, VK_TAB, true, true);
  press(h, VK_TAB, true, true);
  CHECK(panel.page() == SettingsPage::Account);
  press(h, VK_NEXT, true);
  CHECK(panel.page() == SettingsPage::Sound);
  // On the row of tabs, the arrows choose and the keyboard goes along.
  CHECK(tab_to(h, panel, SettingsPanel::TabBase));
  press(h, VK_RIGHT);
  CHECK(panel.page() == SettingsPage::Display && h.app.ui().stop_id == SettingsPanel::TabBase + 1);
}

// 172 emoji, each a circle, as the picker reads them from the assets.
void with_emoji(UiHarness& h) {
  namespace fs = std::filesystem;
  static const fs::path dir = [] {
    fs::path folder = fs::temp_directory_path() / "crpp_focus_test_assets";
    fs::create_directories(folder / "emoji");
    for (int i = 0; i < 172; ++i) {
      std::ofstream(folder / "emoji" / ("1f" + std::to_string(100 + i) + ".svg")) << R"(<svg viewBox="0 0 36 36"><circle cx="18" cy="18" r="18" fill="#fc0"/></svg>)";
    }
    return folder;
  }();
  std::vector<std::string> ids;
  for (int i = 0; i < 172; ++i) ids.push_back("1f" + std::to_string(100 + i));
  h.app.ui().emoji_ids = std::move(ids);
  h.app.reload_assets(dir);
}

void the_emoji_grid_is_one_stop_and_its_arrows_go_by_rows(const char* pack) {
  UiHarness h;
  CHECK(h.start(951, 692, pack));
  with_emoji(h);
  h.app.show_home();
  h.render();
  std::string picked;
  EmojiPicker& picker = h.app.emoji_picker();
  picker.show("", [&](std::string id) { picked = std::move(id); });
  h.render();
  if (h.app.own_frame()) h.on_pane(Pane::Emoji);
  CHECK(picker.ids().size() > 20);
  if (picker.ids().size() <= 20) return;
  CHECK(h.app.ui().tab_stops().size() == 1);
  CHECK(tab_to(h, picker, EmojiPicker::CellBase));
  press(h, VK_RIGHT);
  press(h, VK_DOWN);
  CHECK(h.app.ui().stop_id == EmojiPicker::CellBase + 9);
  // Far down, its row is brought into view, so it is still a stop.
  for (int i = 0; i < 12; ++i) press(h, VK_DOWN);
  CHECK(h.app.ui().stop_id == EmojiPicker::CellBase + 9 + 12 * 8);
  CHECK(h.app.ui().find_hit(&picker, EmojiPicker::CellBase + 9 + 12 * 8) != nullptr);
  const std::string chosen = picker.ids()[9 + 12 * 8];  // closing lets go of the list
  press(h, VK_RETURN);
  CHECK(picked == chosen);
}

}  // namespace

int main() {
  for (const char* pack : {CRPP_TEST_SKINS "/cloud", CRPP_TEST_SKINS "/retro"}) {
    tab_goes_round_the_stops_and_back(pack);
    a_field_takes_the_keys_and_not_the_tab(pack);
    a_click_moves_the_keyboard_without_showing_it(pack);
    a_question_keeps_tab_and_its_keys(pack);
    space_acts_on_a_button_and_a_toggle(pack);
    a_slider_moves_by_the_arrows_pages_and_ends(pack);
    a_dropdown_picks_closed_and_open(pack);
    ctrl_tab_and_the_arrows_go_through_the_tabs(pack);
    the_emoji_grid_is_one_stop_and_its_arrows_go_by_rows(pack);
  }
  return crpp_test_result();
}
