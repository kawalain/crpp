// Home drawn from the Retro pack in skins/: the same places to click as the C++ layout, and
// the same results. Their pixels are compared with tests/shots while both forms exist.
#include <functional>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "check.hpp"
#include "crpp/ui/cursor_image.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
using test::UiHarness;

namespace {

constexpr const char* retro_pack = CRPP_TEST_SKINS "/retro";

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
  h.app.set_theme("luna_blue");
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
  CHECK(h.app.load_skin_dir(retro_pack).has_value());
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
  CHECK(h.app.load_skin_dir(retro_pack).has_value());
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

// The same for every Skin, whether drawn by a pack or by the C++ layouts.
struct Look {
  const char* pack;
  const char* theme;
};

void start_with(UiHarness& h, Look look) {
  CHECK(h.start());
  if (look.pack) CHECK(h.app.load_skin_dir(look.pack).has_value());
  h.app.set_theme(look.theme);
  h.app.show_home();
  h.app.home().set_nickname("Me");
}

void test_a_button_acts_when_the_button_comes_up_on_it(Look look) {
  UiHarness h;
  start_with(h, look);
  HomeScreen& home = h.app.home();
  int found = 0;
  home.on_find_public = [&] { ++found; };
  h.render();
  const Ui::Hit* hit = h.app.ui().find_hit(&home, HomeScreen::Find);
  CHECK(hit != nullptr);
  if (!hit) return;
  const float x = hit->rect.x + hit->rect.w / 2, y = hit->rect.y + hit->rect.h / 2;
  const auto looks_pressed = [&] { return h.app.ui().pressed(&home, HomeScreen::Find); };

  h.app.mouse_move(x, y);
  h.app.mouse_down(x, y, false);
  CHECK(found == 0);
  CHECK(looks_pressed());
  h.app.mouse_up(x, y);
  CHECK(found == 1);
  CHECK(!looks_pressed());

  // Off it the look goes, and back on it the look returns; only there does it act.
  h.app.mouse_down(x, y, false);
  h.app.mouse_move(1, 1);
  CHECK(!looks_pressed());
  h.app.mouse_move(x, y);
  CHECK(looks_pressed());
  h.app.mouse_move(1, 1);
  h.app.mouse_up(1, 1);
  CHECK(found == 1);

  // Coming up on it after going down elsewhere is no click either.
  h.app.mouse_down(1, 1, false);
  h.app.mouse_move(x, y);
  CHECK(!looks_pressed());
  h.app.mouse_up(x, y);
  CHECK(found == 1);

  // A window that loses the mouse never sees the button come up.
  h.app.mouse_move(x, y);
  h.app.mouse_down(x, y, false);
  CHECK(h.app.mouse_cancel());
  CHECK(!looks_pressed());
  h.app.mouse_up(x, y);
  CHECK(found == 1);
}

void test_what_cannot_wait_acts_on_the_press(Look look) {
  UiHarness h;
  start_with(h, look);
  HomeScreen& home = h.app.home();
  h.render();
  const auto down_on = [&](const Surface& owner, int id) {
    const Ui::Hit* hit = h.app.ui().find_hit(&owner, id);
    CHECK(hit != nullptr);
    if (hit) h.app.mouse_down(hit->rect.x + hit->rect.w / 2, hit->rect.y + hit->rect.h / 2, false);
  };

  // A field takes the focus at once, and holds on to the drag.
  down_on(home, HomeScreen::Code);
  CHECK(h.app.ui().focus != nullptr);
  CHECK(h.app.ui().capture == &home);
  h.app.mouse_cancel();

  // A list opens at once, and an option is picked when the button comes up on it.
  SettingsPanel& panel = h.app.settings();
  panel.open(SettingsPage::System);
  if (std::ranges::contains(h.app.open_panes(), Pane::Options)) h.on_pane(Pane::Options);
  h.render();
  SystemTab& system = panel.system();
  std::string before = std::string(h.app.ui().prefs->get(pref::language));
  down_on(system, SystemTab::LanguageList);
  h.app.mouse_cancel();
  h.render();
  const Ui::Hit* option = h.app.ui().find_hit(&system, SystemTab::LanguageList + 3);
  CHECK(option != nullptr);
  if (!option) return;
  const float x = option->rect.x + 2, y = option->rect.y + 2;
  h.app.mouse_down(x, y, false);
  CHECK(h.app.ui().prefs->get(pref::language) == before);
  h.app.mouse_up(x, y);
  CHECK(h.app.ui().prefs->get(pref::language) != before);
}

void test_a_click_in_the_field_lands_in_the_text() {
  UiHarness h;
  start_on_home(h);
  CHECK(h.app.load_skin_dir(retro_pack).has_value());
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
  h.app.mouse_up(x, y);
  h.render();
  CHECK(h.app.ui().caret.x < caret_after_typing.x);
}

void test_a_theme_outside_the_pack_is_drawn_as_before() {
  UiHarness h;
  start_on_home(h);
  CHECK(h.app.load_skin_dir(retro_pack).has_value());
  CHECK(h.app.ui().pack_screen("home") != nullptr);
  h.app.set_theme("luna_blue_dark");
  CHECK(h.app.ui().pack_screen("home") != nullptr);
  h.app.set_theme("cloud");
  CHECK(h.app.ui().pack_screen("home") == nullptr);
  CHECK(h.render());
  h.app.set_theme("luna_blue");
  CHECK(h.app.ui().pack_screen("home") != nullptr);
  CHECK(h.render());
}

void test_graphics_can_be_let_go_with_a_pack_drawn() {
  UiHarness h;
  start_on_home(h);
  CHECK(h.app.load_skin_dir(retro_pack).has_value());
  CHECK(h.render());
  h.drop_target();
  h.app.release_graphics();
  CHECK(h.make_target());
  CHECK(h.render());
}

void test_the_prompt_from_the_pack_takes_typing_and_clicks() {
  UiHarness h;
  CHECK(h.start(420, 400));
  h.app.set_theme("luna_blue");
  CHECK(h.app.load_skin_dir(retro_pack).has_value());
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

void test_a_room_from_the_pack_redraws_only_the_disc() {
  UiHarness h;
  CHECK(h.start(420, 400));
  h.app.set_theme("luna_blue");
  CHECK(h.app.load_skin_dir(retro_pack).has_value());
  h.app.show_session("ABC234");
  h.app.room().set_room({{0, "Me", "", false, "c0"}, {1, "You", "", false, "c1"}}, 0, 0, false, true);
  h.render();
  RoomScreen& room = h.app.room();
  h.app.tick(clock_ms());
  h.render();
  room.pulse(1);
  CHECK(room.speaking_level(1) == 1);
  const auto area = h.app.only_moving_area();
  CHECK(area.has_value());
  if (area) CHECK(area->w == 42 && area->h == 42);  // the picture and its orange
}

void test_the_chat_from_the_pack_scrolls_and_sends() {
  UiHarness h;
  CHECK(h.start(420, 400));
  h.app.set_theme("luna_blue");
  CHECK(h.app.load_skin_dir(retro_pack).has_value());
  h.app.show_session("ABC234");
  h.app.room().set_room({{0, "Me", "", false, "c0"}, {1, "You", "", false, "c1"}}, 0, 0, false, true);
  h.render();
  ChatPanel& chat = h.app.room().chat();
  for (int i = 0; i < 40; ++i) chat.add({1, "c1", "You", "", "hello " + std::to_string(i), 1700000000000});
  h.render();
  const float bottom = chat.scroll_offset();
  CHECK(bottom > 0);
  CHECK(h.click(chat, ChatPanel::BarUp));
  CHECK(chat.scroll_offset() < bottom);
  std::string sent;
  chat.on_send = [&](std::string text) {
    sent = std::move(text);
    return true;
  };
  CHECK(h.click(chat, ChatPanel::Input));
  for (const wchar_t c : std::wstring(L"hi")) h.app.char_input(c, false);
  h.render();
  CHECK(h.click(chat, ChatPanel::Send));
  CHECK(sent == "hi");
}

// Everything the panels offer to click, with where it is. A window's panel is looked at from that window.
struct PanelHit {
  const Surface* owner;
  Pane pane;
  int id;
};

void open_every_panel(UiHarness& h) {
  h.app.show_session("ABC234");
  h.app.room().set_room({{0, "Me", "", false, "c0"}, {1, "You", "", false, "c1"}}, 0, 0, false, true);
  h.app.room().member_menu().open(1, false, 70, 300, 200);
  h.app.toasts().toast("A message", "Do it", [] {});
  h.app.update_popup().show_available();
  h.app.notice().set(Notice{"A longer notice that takes more than one line of this window to say what it wants to say", "Act", "Later"});
  h.app.dialogs().show({"Is this a question that wraps on to a second line of its own?", "Yes", "No", false, nullptr, nullptr});
  RoomScreen& room = h.app.room();
  room.profile_card().request(1);
  room.show_profile({1, "You", "", std::optional<std::string>("you"), std::optional<std::int64_t>(1700000000), 3900, 12345});
  h.app.offline().show();
  h.app.settings().open(SettingsPage::Display);
}

void test_the_panels_from_the_pack_stand_where_the_code_puts_them() {
  UiHarness h;
  CHECK(h.start(640, 500));
  h.app.set_theme("luna_blue");
  open_every_panel(h);
  const NoticeCard& notice = h.app.notice();
  const MemberMenu& menu = h.app.room().member_menu();
  const std::vector<PanelHit> wanted = {
      {&notice, Pane::Main, NoticeCard::Card},
      {&notice, Pane::Main, NoticeCard::Action},
      {&notice, Pane::Main, NoticeCard::Dismiss},
      {&h.app.toasts(), Pane::Main, Toasts::Action},
      {&h.app.update_popup(), Pane::Main, UpdatePopup::Card},
      {&h.app.update_popup(), Pane::Main, UpdatePopup::Button},
      {&menu, Pane::Main, MemberMenu::Card},
      {&menu, Pane::Main, MemberMenu::Profile},
      {&menu, Pane::Main, MemberMenu::Slider},
      {&menu, Pane::Main, MemberMenu::Mute},
      {&h.app.dialogs(), Pane::Dialog, Dialogs::Card},
      {&h.app.dialogs(), Pane::Dialog, Dialogs::Confirm},
      {&h.app.dialogs(), Pane::Dialog, Dialogs::Cancel},
      {&h.app.room().profile_card(), Pane::Profile, ProfileCard::Card},
      {&h.app.offline(), Pane::Offline, OfflinePopup::Card},
  };
  const auto look = [&] {
    std::vector<Rect> found;
    for (const PanelHit& want : wanted) {
      h.on_pane(want.pane);
      const Ui::Hit* hit = h.app.ui().find_hit(want.owner, want.id);
      CHECK(hit != nullptr);
      found.push_back(hit ? hit->rect : Rect{});
    }
    h.on_pane(Pane::Main);
    return found;
  };
  const std::vector<Rect> code = look();
  const PaneInfo dialog = h.app.pane_info(Pane::Dialog), profile = h.app.pane_info(Pane::Profile), offline = h.app.pane_info(Pane::Offline), emoji = h.app.pane_info(Pane::Emoji);
  CHECK(h.app.load_skin_dir(retro_pack).has_value());
  const std::vector<Rect> pack = look();
  for (std::size_t i = 0; i < std::min(pack.size(), code.size()); ++i) {
    CHECK(std::abs(pack[i].x - code[i].x) < 0.01f && std::abs(pack[i].y - code[i].y) < 0.01f);
    CHECK(std::abs(pack[i].w - code[i].w) < 0.01f && std::abs(pack[i].h - code[i].h) < 0.01f);
  }
  // A window of its own is as big as what is in it.
  CHECK(h.app.pane_info(Pane::Dialog).w == dialog.w && h.app.pane_info(Pane::Dialog).h == dialog.h);
  CHECK(h.app.pane_info(Pane::Profile).w == profile.w && h.app.pane_info(Pane::Profile).h == profile.h);
  CHECK(h.app.pane_info(Pane::Offline).w == offline.w && h.app.pane_info(Pane::Offline).h == offline.h);
  CHECK(h.app.pane_info(Pane::Emoji).w == emoji.w && h.app.pane_info(Pane::Emoji).h == emoji.h);
}

void test_the_panels_from_the_pack_do_what_they_do_on_the_code() {
  UiHarness h;
  CHECK(h.start(640, 500));
  h.app.set_theme("luna_blue");
  CHECK(h.app.load_skin_dir(retro_pack).has_value());
  h.app.show_session("ABC234");
  h.app.room().set_room({{0, "Me", "", false, "c0"}, {1, "You", "", false, "c1"}}, 0, 0, false, true);

  // A menu's slider and mute; one's own has a mute that cannot be pressed.
  MemberMenu& menu = h.app.room().member_menu();
  int volume = -1;
  menu.on_volume = [&](std::uint32_t, int percent) { volume = percent; };
  menu.open(1, false, 70, 300, 200);
  h.render();
  CHECK(h.click(menu, MemberMenu::Slider));
  CHECK(volume >= 45 && volume <= 55);  // the slider's middle
  CHECK(h.click(menu, MemberMenu::Mute));
  CHECK(volume == 0);
  menu.close();
  menu.open(0, true, 100, 300, 200);
  h.render();
  CHECK(h.drawn(menu, MemberMenu::Profile));
  CHECK(!h.drawn(menu, MemberMenu::Mute));
  CHECK(!h.drawn(menu, MemberMenu::Slider));
  menu.close();

  // A question and its two answers.
  int answered = 0;
  h.app.dialogs().show({"Sure?", "Yes", "No", false, [&] { answered = 1; }, [&] { answered = 2; }});
  h.on_pane(Pane::Dialog);
  CHECK(h.click(h.app.dialogs(), Dialogs::Cancel));
  CHECK(answered == 2);
  h.app.dialogs().show({"Sure?", "Yes", "", false, [&] { answered = 1; }, nullptr});
  h.render();
  CHECK(!h.drawn(h.app.dialogs(), Dialogs::Cancel));
  CHECK(h.click(h.app.dialogs(), Dialogs::Confirm));
  CHECK(answered == 1);
  h.on_pane(Pane::Main);

  // Messages with something to press.
  int acted = 0;
  h.app.toasts().toast("Saved", "Undo", [&] { ++acted; });
  h.render();
  CHECK(h.click(h.app.toasts(), Toasts::Action));
  CHECK(acted == 1);
  h.app.notice().on_action = [&] { ++acted; };
  h.app.notice().set(Notice{"News", "Act", "Later"});
  h.render();
  CHECK(h.click(h.app.notice(), NoticeCard::Action));
  CHECK(acted == 2);
  CHECK(!h.drawn(h.app.notice(), NoticeCard::Dismiss));
  h.app.update_popup().on_update = [&] { ++acted; };
  h.app.update_popup().show_available();
  h.render();
  CHECK(h.click(h.app.update_popup(), UpdatePopup::Button));
  CHECK(acted == 3);
  h.app.update_popup().show_progress(40);
  h.render();
  CHECK(!h.drawn(h.app.update_popup(), UpdatePopup::Button));
  h.app.update_popup().hide();

  // Someone's own card changes the icon from it, and nobody else's does.
  RoomScreen& room = h.app.room();
  ProfileCard& card = room.profile_card();
  int changing = 0;
  card.on_change_icon = [&] { ++changing; };
  card.request(1);
  room.show_profile({1, "You", "", std::nullopt, std::nullopt, 60, 1});
  h.on_pane(Pane::Profile);
  CHECK(!h.drawn(card, ProfileCard::Icon));
  card.close();
  card.request(0);
  room.show_profile({0, "Me", "", std::nullopt, std::nullopt, 60, 1});
  h.render();
  CHECK(h.click(card, ProfileCard::Icon));
  CHECK(changing == 1);
  card.close();
  h.on_pane(Pane::Main);
}

void test_the_emoji_from_the_pack_walk_only_what_is_in_view() {
  UiHarness h;
  CHECK(h.start(640, 500));
  h.app.set_theme("luna_blue");
  CHECK(h.app.load_skin_dir(retro_pack).has_value());
  h.app.show_session("ABC234");
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "crpp_skin_emoji_test";
  std::filesystem::create_directories(dir / "emoji");
  std::vector<std::string> ids;
  for (int i = 0; i < 400; ++i) {
    ids.push_back("1f" + std::to_string(1000 + i));
    std::ofstream(dir / "emoji" / (ids.back() + ".svg")) << R"(<svg viewBox="0 0 36 36"><circle cx="18" cy="18" r="18" fill="#fc0"/></svg>)";
  }
  h.app.ui().emoji_ids = ids;
  h.app.reload_assets(dir);
  EmojiPicker& picker = h.app.emoji_picker();
  std::string picked;
  picker.show(ids[2], [&](std::string id) { picked = std::move(id); });
  h.on_pane(Pane::Emoji);
  CHECK(picker.ids().size() == 400);
  CHECK(h.drawn(picker, EmojiPicker::CellBase));
  CHECK(!h.drawn(picker, EmojiPicker::CellBase + 399));
  CHECK(picker.parsed() <= EmojiPicker::cache_capacity);
  CHECK(h.drawn(picker, EmojiPicker::Thumb));
  for (int i = 0; i < 200; ++i) picker.wheel(0, 0, -1);
  h.app.tick(clock_ms());
  h.render();
  CHECK(!h.drawn(picker, EmojiPicker::CellBase));
  CHECK(h.drawn(picker, EmojiPicker::CellBase + 399));
  CHECK(picker.parsed() <= EmojiPicker::cache_capacity);
  const std::string last = picker.ids()[399];
  CHECK(h.click(picker, EmojiPicker::CellBase + 399));
  CHECK(picked == last);
  h.on_pane(Pane::Main);
}

void test_a_list_from_the_pack_picks_and_the_notes_scroll() {
  UiHarness h;
  CHECK(h.start(640, 500));
  h.app.set_theme("luna_blue");
  CHECK(h.app.load_skin_dir(retro_pack).has_value());
  h.app.show_home();

  h.app.settings().open(SettingsPage::Display);
  h.on_pane(Pane::Options);
  DisplayTab& display = h.app.settings().display();
  CHECK(h.click(display, DisplayTab::SkinThemeList));
  CHECK(h.drawn(display, DisplayTab::SkinThemeList + 1));
  CHECK(h.click(display, DisplayTab::SkinThemeList + 2));
  CHECK(!h.drawn(display, DisplayTab::SkinThemeList + 1));
  h.app.settings().close();
  h.on_pane(Pane::Main);

  // Long notes have a bar, and scrolling on past their end goes to the next version.
  ChangelogEntry entry{"A new version", "2026.11.08", "v1.0.0", {}};
  for (int i = 0; i < 60; ++i) entry.blocks.push_back({crpp::update::NoteKind::Bullet, 0, "", {{"line " + std::to_string(i), i == 0}}});
  ChangelogView& view = h.app.changelog();
  view.open({entry, entry}, 0);
  h.on_pane(Pane::Changelog);
  CHECK(h.drawn(view, ChangelogView::Card));
  CHECK(h.drawn(view, ChangelogView::BarTrack));
  CHECK(h.drawn(view, ChangelogView::BarThumb));
  CHECK(!h.drawn(view, ChangelogView::Close));
  const float top = h.app.ui().find_hit(&view, ChangelogView::BarThumb)->rect.y;
  for (int i = 0; i < 5; ++i) view.wheel(0, 0, -1);
  h.render();
  CHECK(h.app.ui().find_hit(&view, ChangelogView::BarThumb)->rect.y > top);
  for (int i = 0; i < 200 && view.index() == 0; ++i) {
    view.wheel(0, 0, -1);
    h.render();
  }
  CHECK(view.index() == 1);
  view.close();
  view.open({}, 0);
  h.render();
  CHECK(h.drawn(view, ChangelogView::Card));
  CHECK(!h.drawn(view, ChangelogView::BarThumb));
  view.close();
  h.on_pane(Pane::Main);
}

// A copy of the Retro pack with one piece of it changed.
std::filesystem::path pack_with(std::string_view file, std::string_view from, std::string_view to) {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "crpp_skin_changed_pack";
  fs::remove_all(dir);
  fs::copy(retro_pack, dir, fs::copy_options::recursive);
  std::ifstream in(dir / file, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  const auto at = text.find(from);
  CHECK(at != std::string::npos);
  text.replace(at, from.size(), to);
  std::ofstream(dir / file, std::ios::binary) << text;
  return dir;
}

void test_a_theme_brings_its_overlay() {
  UiHarness h;
  CHECK(h.start(640, 500));
  CHECK(h.app.load_skin_dir(retro_pack).has_value());
  h.app.set_theme("classic");
  h.app.show_home();
  h.render();
  CHECK(h.app.ui().palette >= 0);
  const float classic_caption = h.app.frame_metrics().caption_h;
  h.app.set_theme("luna_blue");
  h.render();
  CHECK(h.app.ui().palette >= 0);
  CHECK(h.app.frame_metrics().caption_h != classic_caption);
  // Within one overlay, nothing has to be read again; each of its Themes finds its Palette.
  for (const std::string_view id : {"luna_blue_dark", "luna_homestead", "luna_homestead_dark", "luna_metallic", "luna_metallic_dark"}) {
    h.app.set_theme(id);
    h.render();
    CHECK(h.app.ui().palette >= 0);
  }
  h.app.set_theme("classic_dark");
  h.render();
  CHECK(h.app.ui().palette >= 0);
  CHECK(h.app.frame_metrics().caption_h == classic_caption);
  // A Theme of no pack is drawn by the program.
  h.app.set_theme("cumulus");
  h.render();
  CHECK(h.app.ui().palette < 0);
}

void test_the_pack_says_how_windows_are_measured() {
  UiHarness h;
  CHECK(h.start(640, 500));
  h.app.set_theme("luna_blue");
  h.app.show_home();
  CHECK(h.app.load_skin_dir(pack_with("overlays/luna/pack.jsonc", R"("caption_h": 28)", R"("caption_h": 40)")).has_value());
  CHECK(h.app.frame_metrics().caption_h == 40);
  CHECK(WindowFrame::zone_at(100, 35, 640, 500, {}, h.app.frame_metrics()) == WindowFrame::Zone::Caption);
  h.render();
  CHECK(h.app.ui().content.y == 60);
  CHECK(h.app.load_skin_dir(pack_with("pack.jsonc", R"("window_home_w": 300)", R"("window_home_w": 333)")).has_value());
  CHECK(h.app.screen_size().w == 333);
  CHECK(h.app.load_skin_dir(pack_with("pack.jsonc", R"("window_options_min_h": 440)", R"("window_options_min_h": 444)")).has_value());
  CHECK(h.app.pane_info(Pane::Options).min_h == 444);
  CHECK(h.app.load_skin_dir(pack_with("overlays/luna/pack.jsonc", R"("korean_size": 12)", R"("korean_size": 15)")).has_value());
  h.app.ui().gfx.set_language(L"ko");
  CHECK(h.app.ui().gfx.classic_size() == 15);
  // A pack that moves says the numbers of what moves in the engine.
  CHECK(!h.app.load_skin_dir(pack_with("pack.jsonc", R"("moves": 0)", R"("moves": 1)")).has_value());
  CHECK(h.app.load_skin_dir(pack_with("pack.jsonc", R"("moves": 0)",
                                      R"("moves": 1, "scroll_settle_ms": 10, "scroll_return_ms": 20, "scroll_stretch": 30, "edge_fade": 9, "screen_fade_ms": 40, "screen_rise": 2, "speaking_hold_ms": 50, "speaking_fade_ms": 60)")).has_value());
  CHECK(h.app.ui().skin().moves());
  CHECK(h.app.ui().motion.scroll_stretch == 30);
  CHECK(h.app.ui().motion.speaking_fade_ms == 60);

  // A number the program reads may not be missing.
  const auto refused = h.app.load_skin_dir(pack_with("pack.jsonc", R"("window_room_h": 500, )", ""));
  CHECK(!refused.has_value());
  if (!refused) CHECK(refused.error().find("window_room_h") != std::string::npos);
  std::error_code ignored;
  std::filesystem::remove_all(std::filesystem::temp_directory_path() / "crpp_skin_changed_pack", ignored);
}

// What a system cursor looks like at a point, drawn over a colour.
std::uint32_t drawn_over(HCURSOR cursor, int size, std::uint32_t background, int x, int y) {
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof info.bmiHeader;
  info.bmiHeader.biWidth = size;
  info.bmiHeader.biHeight = -size;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ old = SelectObject(dc, bitmap);
  auto* const pixels = static_cast<std::uint32_t*>(bits);
  std::fill_n(pixels, size * size, background);
  DrawIconEx(dc, 0, 0, cursor, size, size, 0, nullptr, DI_NORMAL);
  GdiFlush();
  const std::uint32_t found = pixels[y * size + x] & 0xFFFFFF;
  SelectObject(dc, old);
  DeleteObject(bitmap);
  DeleteDC(dc);
  return found;
}

void test_the_cursors_of_the_pack_become_system_cursors() {
  UiHarness h;
  CHECK(h.start(640, 500));
  h.app.set_theme("luna_blue");
  CHECK(!make_cursor_image(h.app.ui(), crpp::skin::CursorKind::Arrow, 32).has_value());
  CHECK(!h.app.theme_cursors());
  CHECK(h.app.load_skin_dir(retro_pack).has_value());
  CHECK(h.app.theme_cursors());

  for (std::size_t i = 0; i < crpp::skin::cursor_names.size(); ++i) {
    const auto image = make_cursor_image(h.app.ui(), static_cast<crpp::skin::CursorKind>(i), 32);
    CHECK(image.has_value());
    if (!image) continue;
    CHECK(std::ranges::any_of(image->pixels, [](std::uint32_t pixel) { return (pixel >> 24) == 255; }));
    CHECK(std::ranges::any_of(image->pixels, [](std::uint32_t pixel) { return pixel == 0; }));
    HCURSOR cursor = make_system_cursor(*image);
    CHECK(cursor != nullptr);
    if (!cursor) continue;
    ICONINFO icon{};
    CHECK(GetIconInfo(cursor, &icon));
    CHECK(!icon.fIcon);
    CHECK(static_cast<int>(icon.xHotspot) == image->hot_x && static_cast<int>(icon.yHotspot) == image->hot_y);
    DeleteObject(icon.hbmMask);
    DeleteObject(icon.hbmColor);
    DestroyCursor(cursor);
  }

  // The white arrow, black at its edge, and nothing around it, whatever is behind.
  const auto arrow = make_cursor_image(h.app.ui(), crpp::skin::CursorKind::Arrow, 32);
  CHECK(arrow.has_value());
  if (arrow) {
    CHECK(arrow->hot_x == 0 && arrow->hot_y == 0);
    HCURSOR cursor = make_system_cursor(*arrow);
    CHECK(drawn_over(cursor, 32, 0x000000, 4, 10) == 0xFFFFFF);
    CHECK(drawn_over(cursor, 32, 0xFFFFFF, 4, 10) == 0xFFFFFF);
    CHECK(drawn_over(cursor, 32, 0x123456, 30, 30) == 0x123456);
    DestroyCursor(cursor);
    // Hard edges: every pixel is whole, and the corner of the arrow climbs by one step of the grid.
    CHECK(std::ranges::all_of(arrow->pixels, [](std::uint32_t pixel) { return (pixel >> 24) == 0 || (pixel >> 24) == 255; }));
    const auto at = [&](int x, int y) { return arrow->pixels[static_cast<std::size_t>(y * 32 + x)] >> 24; };
    CHECK(at(0, 0) == 255 && at(1, 0) == 0 && at(1, 1) == 255 && at(2, 1) == 0);
  }
  // Twice the size is the same picture with each pixel twice as wide: the steps grow with it.
  const auto doubled = make_cursor_image(h.app.ui(), crpp::skin::CursorKind::Arrow, 64);
  CHECK(arrow.has_value() && doubled.has_value());
  if (arrow && doubled) {
    bool same = true;
    for (int y = 0; y < 64; ++y) {
      for (int x = 0; x < 64; ++x) {
        same = same && doubled->pixels[static_cast<std::size_t>(y * 64 + x)] == arrow->pixels[static_cast<std::size_t>(y / 2 * 32 + x / 2)];
      }
    }
    CHECK(same);
  }
  {
    // A pixel partly covered, as a pack without hard edges has them, comes out as it was drawn: its colour is not darkened twice.
    CursorImage soft;
    soft.size = 32;
    soft.pixels.assign(32 * 32, 0);
    soft.pixels[5 * 32 + 5] = 0x80808080;
    HCURSOR cursor = make_system_cursor(soft);
    CHECK(cursor != nullptr);
    const std::uint32_t over_black = drawn_over(cursor, 32, 0x000000, 5, 5) & 255;
    CHECK(over_black + 2 >= 0x80 && over_black <= 0x80 + 2);
    DestroyCursor(cursor);
  }
  const auto larger = make_cursor_image(h.app.ui(), crpp::skin::CursorKind::IBeam, 48);
  CHECK(larger.has_value());
  if (larger) CHECK(larger->size == 48 && larger->hot_x == 24 && larger->hot_y == 24);

  // The check in the display tab, for a Theme with cursors.
  h.app.show_home();
  h.app.settings().open(SettingsPage::Display);
  h.on_pane(Pane::Options);
  DisplayTab& display = h.app.settings().display();
  CHECK(h.click(display, DisplayTab::ThemeCursor));
  CHECK(!h.app.theme_cursors());
  CHECK(h.click(display, DisplayTab::ThemeCursor));
  CHECK(h.app.theme_cursors());
  h.app.settings().close();
  h.on_pane(Pane::Main);
}

// What is drawn in a rectangle, to tell two looks of it apart.
std::uint32_t look_of(UiHarness& h, Rect r) {
  std::uint32_t sum = 0;
  for (int y = static_cast<int>(r.y); y < static_cast<int>(r.bottom()); ++y) {
    for (int x = static_cast<int>(r.x); x < static_cast<int>(r.right()); ++x) sum = sum * 31 + h.pixel(x, y);
  }
  return sum;
}

// It looks pressed and scrolls again and again while the pointer stays on it.
void hold_an_arrow(UiHarness& h, const Surface& owner, int arrow, const std::function<float()>& position) {
  const Ui::Hit* hit = h.app.ui().find_hit(&owner, arrow);
  CHECK(hit != nullptr);
  if (!hit) return;
  const Rect rect = hit->rect;
  const float x = rect.x + rect.w / 2, y = rect.y + rect.h / 2;
  const auto looks_pressed = [&] { return h.app.ui().pressed(&owner, arrow); };
  h.app.mouse_move(1, 1);
  h.render();
  const std::uint32_t away = look_of(h, rect);
  h.app.mouse_move(x, y);
  h.render();
  const std::uint32_t up = look_of(h, rect);

  const float start = position();
  h.app.mouse_down(x, y, false);
  h.render();
  CHECK(looks_pressed());
  CHECK(look_of(h, rect) != up);
  const float first = position();
  CHECK(first != start);
  CHECK(h.app.hold_wait_ms() == 400);
  CHECK(h.app.hold_repeat_ms() == 50);
  CHECK(h.app.hold_step());
  h.render();
  const float second = position();
  CHECK(second != first);

  // Off it, it rests; back on it, it goes on.
  h.app.mouse_move(1, 1);
  h.render();
  CHECK(!looks_pressed());
  CHECK(look_of(h, rect) == away);
  CHECK(!h.app.hold_step());
  CHECK(position() == second);
  h.app.mouse_move(x, y);
  h.render();
  CHECK(looks_pressed());
  CHECK(h.app.hold_step());
  h.render();
  const float third = position();
  CHECK(third != second);

  // The press moved it, so the button coming up does not.
  h.app.mouse_up(x, y);
  h.render();
  CHECK(position() == third);
  CHECK(h.app.hold_wait_ms() == 0);
  CHECK(!h.app.hold_step());
  CHECK(look_of(h, rect) == up);
}

void test_the_arrows_of_every_scrollbar_can_be_held(const char* theme) {
  UiHarness h;
  CHECK(h.start(640, 360));
  CHECK(h.app.load_skin_dir(retro_pack).has_value());
  h.app.set_theme(theme);

  h.app.show_session("ABC234");
  std::vector<MemberView> members;
  for (std::uint32_t slot = 0; slot < 16; ++slot) members.push_back({slot, "Member", "", false, "c" + std::to_string(slot)});
  RoomScreen& room = h.app.room();
  room.set_room(members, 0, 0, false, true);
  h.render();
  ChatPanel& chat = room.chat();
  for (int i = 0; i < 40; ++i) chat.add({1, "c1", "You", "", "hello " + std::to_string(i), 1700000000000});
  h.render();
  hold_an_arrow(h, chat, ChatPanel::BarUp, [&] { return chat.scroll_offset(); });
  hold_an_arrow(h, room, RoomScreen::MembersDown, [&] { return room.members_offset(); });
  hold_an_arrow(h, room, RoomScreen::MembersUp, [&] { return room.members_offset(); });

  h.app.show_home();
  SettingsPanel& panel = h.app.settings();
  panel.open(SettingsPage::Sound);
  h.on_pane(Pane::Options);
  hold_an_arrow(h, panel, SettingsPanel::BarDown, [&] { return panel.scroll_offset(); });
  hold_an_arrow(h, panel, SettingsPanel::BarUp, [&] { return panel.scroll_offset(); });

  // Held on the track it goes by pages, until the thumb comes to the pointer.
  panel.scroll_to(0);
  h.render();
  const Ui::Hit* track = h.app.ui().find_hit(&panel, SettingsPanel::Track);
  CHECK(track != nullptr);
  if (track) {
    const float x = track->rect.x + track->rect.w / 2, y = track->rect.bottom() - 24;
    h.app.mouse_move(x, y);
    h.app.mouse_down(x, y, false);
    h.render();
    for (int i = 0; i < 100 && h.app.hold_step(); ++i) h.render();
    CHECK(!h.app.hold_step());
    const Ui::Hit* thumb = h.app.ui().find_hit(&panel, SettingsPanel::Thumb);
    CHECK(thumb != nullptr);
    if (thumb) CHECK(thumb->rect.contains(x, y));
    h.app.mouse_up(x, y);
  }
  panel.close();
  h.on_pane(Pane::Main);

  ChangelogEntry entry{"A new version", "2026.11.08", "v1.0.0", {}};
  for (int i = 0; i < 60; ++i) entry.blocks.push_back({crpp::update::NoteKind::Bullet, 0, "", {{"line " + std::to_string(i), i == 0}}});
  ChangelogView& view = h.app.changelog();
  view.open({entry}, 0);
  h.on_pane(Pane::Changelog);
  hold_an_arrow(h, view, ChangelogView::BarDown, [&] { return view.scroll_offset(); });
  hold_an_arrow(h, view, ChangelogView::BarUp, [&] { return view.scroll_offset(); });
  view.close();
  h.on_pane(Pane::Main);
}

void test_a_window_of_its_own_has_the_background_of_its_theme() {
  UiHarness h;
  CHECK(h.start(640, 500));
  CHECK(h.app.load_skin_dir(retro_pack).has_value());
  h.app.dialogs().show({"A question", "Yes", "No", false, nullptr, nullptr});
  const auto background = [&](std::string_view theme) {
    h.app.set_theme(theme);
    h.on_pane(Pane::Main);
    h.on_pane(Pane::Dialog);
    const Ui::Hit* card = h.app.ui().find_hit(&h.app.dialogs(), Dialogs::Card);
    CHECK(card != nullptr);
    return card ? h.pixel(static_cast<int>(card->rect.x) + 2, static_cast<int>(card->rect.bottom()) - 2) : 0u;
  };
  CHECK(background("classic") == 0xc0c0c0);
  CHECK(background("classic_dark") == 0x2e2e2e);
  CHECK(background("luna_blue") == 0xece9d8);
  CHECK(background("luna_blue_dark") == 0x232833);
  CHECK(background("luna_metallic") == 0xe0dfe3);
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
  for (const Look look : {Look{nullptr, "cloud"}, Look{nullptr, "luna_blue"}, Look{retro_pack, "classic"}, Look{CRPP_TEST_SKINS "/cloud", "cloud"}}) {
    test_a_button_acts_when_the_button_comes_up_on_it(look);
    test_what_cannot_wait_acts_on_the_press(look);
  }
  test_a_click_in_the_field_lands_in_the_text();
  test_a_theme_outside_the_pack_is_drawn_as_before();
  test_graphics_can_be_let_go_with_a_pack_drawn();
  test_the_prompt_from_the_pack_takes_typing_and_clicks();
  test_a_room_from_the_pack_redraws_only_the_disc();
  test_the_chat_from_the_pack_scrolls_and_sends();
  test_the_panels_from_the_pack_stand_where_the_code_puts_them();
  test_the_panels_from_the_pack_do_what_they_do_on_the_code();
  test_the_emoji_from_the_pack_walk_only_what_is_in_view();
  test_a_list_from_the_pack_picks_and_the_notes_scroll();
  test_a_theme_brings_its_overlay();
  test_the_pack_says_how_windows_are_measured();
  test_the_cursors_of_the_pack_become_system_cursors();
  test_a_window_of_its_own_has_the_background_of_its_theme();
  test_the_arrows_of_every_scrollbar_can_be_held("classic");
  test_the_arrows_of_every_scrollbar_can_be_held("luna_blue");
  test_a_folder_without_a_pack_is_refused_and_changes_nothing();
  return crpp_test_result();
}
