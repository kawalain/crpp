// Home drawn from the Messenger pack in skins/: the same places to click as the C++ layout, and
// the same results. Their pixels are compared with tests/shots while both forms exist.
#include <cmath>
#include <filesystem>
#include <fstream>
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

void test_a_room_from_the_pack_redraws_only_the_disc() {
  UiHarness h;
  CHECK(h.start(420, 400));
  h.app.set_theme("messenger");
  CHECK(h.app.load_skin_dir(messenger_pack).has_value());
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
  h.app.set_theme("messenger");
  CHECK(h.app.load_skin_dir(messenger_pack).has_value());
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
  h.app.settings().display().click(DisplayTab::ThemeList, 0, 0, false);
}

void test_the_panels_from_the_pack_stand_where_the_code_puts_them() {
  UiHarness h;
  CHECK(h.start(640, 500));
  h.app.set_theme("messenger");
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
      {&h.app.settings().display(), Pane::Options, DisplayTab::ThemeList + 1},
      {&h.app.settings().display(), Pane::Options, DisplayTab::ThemeList + 2},
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
  CHECK(h.app.load_skin_dir(messenger_pack).has_value());
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
  h.app.set_theme("messenger");
  CHECK(h.app.load_skin_dir(messenger_pack).has_value());
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
  h.app.mouse_up();
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
  h.app.set_theme("messenger");
  CHECK(h.app.load_skin_dir(messenger_pack).has_value());
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
  h.app.set_theme("messenger");
  CHECK(h.app.load_skin_dir(messenger_pack).has_value());
  h.app.show_home();

  h.app.settings().open(SettingsPage::Display);
  h.on_pane(Pane::Options);
  DisplayTab& display = h.app.settings().display();
  CHECK(h.click(display, DisplayTab::ThemeList));
  CHECK(h.drawn(display, DisplayTab::ThemeList + 1));
  CHECK(h.click(display, DisplayTab::ThemeList + 2));
  CHECK(!h.drawn(display, DisplayTab::ThemeList + 1));
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
  test_a_room_from_the_pack_redraws_only_the_disc();
  test_the_chat_from_the_pack_scrolls_and_sends();
  test_the_panels_from_the_pack_stand_where_the_code_puts_them();
  test_the_panels_from_the_pack_do_what_they_do_on_the_code();
  test_the_emoji_from_the_pack_walk_only_what_is_in_view();
  test_a_list_from_the_pack_picks_and_the_notes_scroll();
  test_a_folder_without_a_pack_is_refused_and_changes_nothing();
  return crpp_test_result();
}
