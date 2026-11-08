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

// Every Skin's Themes are more than Cloud's open list shows at once: the keyboard's option comes into view.
void an_open_list_scrolls_to_the_keyboard() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  CHECK(h.app.find_skins(CRPP_TEST_SKINS));
  h.app.apply_skin_choice(false);
  open_settings(h, SettingsPage::Display);
  DisplayTab& tab = h.app.settings().display();
  CHECK(tab_to(h, tab, DisplayTab::SkinThemeList));
  press(h, VK_SPACE);
  // Cloud's heading and two Themes, then Retro's heading and four.
  const int first = DisplayTab::SkinThemeList + 2, last = DisplayTab::SkinThemeList + 8;
  CHECK(h.drawn(tab, first) && !h.drawn(tab, last));
  const auto whole = [&](int id) {
    const Ui::Hit* hit = h.app.ui().find_hit(&tab, id);
    return hit && hit->rect.h >= hit->whole.h - 0.5f;
  };
  press(h, VK_END);
  CHECK(whole(last));
  press(h, VK_HOME);
  CHECK(whole(first));
}

// More members than the list shows: the one the keyboard goes to comes into view.
void the_member_list_scrolls_to_the_keyboard(const char* pack) {
  UiHarness h;
  CHECK(h.start(951, 692, pack));
  h.app.show_session("ABC234");
  RoomScreen& room = h.app.room();
  std::vector<MemberView> members;
  for (std::uint32_t i = 0; i < 40; ++i) members.push_back({i, "m" + std::to_string(i), "", false, "c" + std::to_string(i)});
  room.set_room(members, 0, 0, false, true);
  h.render();
  h.render();
  const auto whole = [&](std::uint32_t slot) {
    const Ui::Hit* hit = h.app.ui().find_hit(&room, RoomScreen::MemberBase + static_cast<int>(slot));
    return hit && hit->rect.h >= hit->whole.h - 0.5f;
  };
  CHECK(!whole(39));
  CHECK(tab_to(h, room, RoomScreen::MemberBase));
  press(h, VK_END);
  CHECK(whole(39));
  press(h, VK_HOME);
  CHECK(whole(0));
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

void a_question_starts_on_cancel_and_gives_the_keyboard_back(const char* pack) {
  UiHarness h;
  CHECK(h.start(951, 692, pack));
  open_settings(h, SettingsPage::System);
  SystemTab& tab = h.app.settings().system();
  // At the end of the tab, out of view: Tab brings it in.
  CHECK(!h.drawn(tab, SystemTab::Reset) || h.app.own_frame());
  CHECK(tab_to(h, tab, SystemTab::Reset));
  h.render();
  CHECK(h.drawn(tab, SystemTab::Reset));
  // Resetting cannot be undone: the question starts on Cancel.
  press(h, VK_RETURN);
  CHECK(h.app.dialogs().is_open());
  if (h.app.own_frame()) h.on_pane(Pane::Dialog);
  h.render();
  CHECK(h.app.ui().stop_owner == &h.app.dialogs() && h.app.ui().stop_id == Dialogs::Cancel);
  press(h, VK_RETURN);
  CHECK(!h.app.dialogs().is_open());
  // Closed, the keyboard is back on what opened it.
  if (h.app.own_frame()) h.on_pane(Pane::Options);
  h.render();
  CHECK(h.app.ui().stop_owner == &tab && h.app.ui().stop_id == SystemTab::Reset);
}

void a_panel_opens_on_its_tab_and_closes_back(const char* pack) {
  UiHarness h;
  CHECK(h.start(951, 692, pack));
  h.app.show_home();
  h.render();
  tab(h);
  const Surface* before = h.app.ui().stop_owner;
  const int before_id = h.app.ui().stop_id;
  open_settings(h, SettingsPage::Display);
  CHECK(h.app.ui().stop_owner == &h.app.settings() && h.app.ui().stop_id == SettingsPanel::TabBase + 1);
  press(h, VK_ESCAPE);
  if (h.app.own_frame()) h.app.use_pane(Pane::Main);
  h.render();
  CHECK(h.app.ui().stop_owner == before && h.app.ui().stop_id == before_id);
}

void the_messages_and_the_members_take_the_arrows(const char* pack) {
  UiHarness h;
  CHECK(h.start(951, 692, pack));
  h.app.show_session("ABC234");
  RoomScreen& room = h.app.room();
  room.set_room({{0, "a", "", false, "c0"}, {1, "b", "", false, "c1"}, {2, "c", "", false, "c2"}}, 0, 0, false, true);
  ChatPanel& chat = room.chat();
  chat.add({0, "c0", "a", "", "one", 1700000000000});
  chat.add({1, "c1", "b", "", "two", 1700000000000});
  chat.add({0, "c0", "a", "", "three", 1700000000000});
  h.render();
  h.render();
  // The messages are one stop; the first arrow is on the last, and Shift takes in those it passes.
  CHECK(tab_to(h, chat, ChatPanel::Area));
  press(h, VK_UP);
  CHECK(chat.selected_text() == L"three");
  press(h, VK_UP);
  CHECK(chat.selected_text() == L"two");
  press(h, VK_UP, false, true);
  const std::wstring both = chat.selected_text();
  CHECK(both.find(L"one") != std::wstring::npos && both.find(L"two") != std::wstring::npos && both.find(L"three") == std::wstring::npos);
  press(h, VK_END);
  CHECK(chat.selected_text() == L"three");
  // The members are one stop too, and the arrows go along them.
  CHECK(tab_to(h, room, RoomScreen::MemberBase));
  press(h, VK_RIGHT);
  CHECK(h.app.ui().current_stop() && h.app.ui().current_stop()->id == RoomScreen::MemberBase + 1);
  // The menu key does what a right-click does: the member's menu, which has the keyboard until it closes.
  press(h, VK_APPS);
  CHECK(room.member_menu().is_open());
  CHECK(h.app.ui().stop_owner == &room.member_menu());
  press(h, VK_ESCAPE);
  CHECK(!room.member_menu().is_open());
  CHECK(h.app.ui().current_stop() && h.app.ui().current_stop()->owner == &room && h.app.ui().current_stop()->id == RoomScreen::MemberBase + 1);
}

// Alt or F10 down and up, as the system sends them.
bool alone(UiHarness& h, unsigned vk) {
  h.app.sys_key(vk, false);
  const bool kept = h.app.sys_key_up(vk);
  h.render();
  return kept;
}

void the_menu_bar_takes_alt_and_its_letters(const char* pack) {
  UiHarness h;
  CHECK(h.start(951, 692, pack));
  h.app.show_home();
  h.render();
  // Tab shows where the keyboard is and Alt the underlines of access keys too, as in Windows.
  tab(h);
  CHECK(h.app.ui().cues && !h.app.ui().access_cues);
  if (!h.app.own_frame()) {
    // A frame of the system's keeps Alt's way into its own menu.
    CHECK(!alone(h, VK_MENU));
    CHECK(h.app.ui().access_cues);
    return;
  }
  WindowFrame& frame = h.app.frame();
  int options = 0, changelog = 0;
  frame.on_options = [&] { ++options; };
  frame.on_changelog = [&] { ++changelog; };
  // Alt alone is on the first title; Right goes to Tools, Down opens it on Options, Enter runs it.
  CHECK(alone(h, VK_MENU));
  CHECK(frame.keyboard_in_menus() && h.app.ui().access_cues);
  press(h, VK_RIGHT);
  press(h, VK_DOWN);
  press(h, VK_RETURN);
  CHECK(options == 1);
  CHECK(!frame.keyboard_in_menus());
  // Alt and a title's letter opens it, and the system is not told the character; the item's letter runs it.
  CHECK(h.app.sys_key('H', false));
  CHECK(h.app.take_sys_char(L'h'));
  CHECK(!h.app.take_sys_char(L'h'));
  CHECK(h.app.sys_key_up(VK_MENU));
  h.render();
  CHECK(frame.keyboard_in_menus());
  press(h, 'W');
  CHECK(h.app.char_input(L'w', false));  // not typed anywhere
  CHECK(changelog == 1);
  // A letter no title has is left to the system, as Alt+Space.
  CHECK(!h.app.sys_key('Q', false));
  CHECK(!h.app.take_sys_char(L' '));
  // Escape closes the menu and then leaves the bar; Alt leaves it at once.
  CHECK(alone(h, VK_F10));
  press(h, VK_DOWN);
  press(h, VK_ESCAPE);
  CHECK(frame.keyboard_in_menus());
  press(h, VK_ESCAPE);
  CHECK(!frame.keyboard_in_menus());
  CHECK(alone(h, VK_MENU));
  CHECK(alone(h, VK_MENU));
  CHECK(!frame.keyboard_in_menus());
  // Alt with a click or another key between is not alone.
  h.app.sys_key(VK_MENU, false);
  h.app.mouse_down(200, 12, false);
  h.app.mouse_up(200, 12);
  CHECK(h.app.sys_key_up(VK_MENU));
  h.render();
  CHECK(!frame.keyboard_in_menus());
  // In the menus, Alt+F4 is still the system's, and leaves them.
  CHECK(alone(h, VK_MENU));
  CHECK(!h.app.sys_key(VK_F4, false));
  CHECK(!frame.keyboard_in_menus());
  CHECK(options == 1 && changelog == 1);
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
    a_question_starts_on_cancel_and_gives_the_keyboard_back(pack);
    a_panel_opens_on_its_tab_and_closes_back(pack);
    the_messages_and_the_members_take_the_arrows(pack);
    the_menu_bar_takes_alt_and_its_letters(pack);
    the_member_list_scrolls_to_the_keyboard(pack);
  }
  an_open_list_scrolls_to_the_keyboard();
  return crpp_test_result();
}
