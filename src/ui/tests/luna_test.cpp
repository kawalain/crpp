// The Luna Skin: its own frame and window sizes, and that its layouts reach the same state and
// callbacks as Cloud's.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "check.hpp"
#include "crpp/ui/luna.hpp"
#include "crpp/ui/settings_tabs.hpp"
#include "crpp/ui/themes.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
using test::UiHarness;

namespace {

constexpr unsigned width = 640, height = 500;

void start_luna(UiHarness& h) {
  CHECK(h.start(width, height));
  h.app.set_theme("messenger");
  h.render();
}

void room_with_chat(UiHarness& h) {
  h.app.show_session("ABC234");
  h.app.room().set_room({{0, "나", "", false, "c0"}, {1, "너", "", false, "c1"}}, 0, 0, false, true);
  h.render();
}

void test_points_of_the_frame_go_to_the_system() {
  using Zone = WindowFrame::Zone;
  constexpr float w = 300, h = 560;
  CHECK(WindowFrame::zone_at(100, 10, w, h) == Zone::Caption);
  CHECK(WindowFrame::zone_at(100, 100, w, h) == Zone::Client);
  CHECK(WindowFrame::zone_at(0, 100, w, h) == Zone::Left);
  CHECK(WindowFrame::zone_at(w - 1, 100, w, h) == Zone::Right);
  CHECK(WindowFrame::zone_at(100, 0, w, h) == Zone::Top);
  CHECK(WindowFrame::zone_at(100, h - 1, w, h) == Zone::Bottom);
  CHECK(WindowFrame::zone_at(0, 0, w, h) == Zone::TopLeft);
  CHECK(WindowFrame::zone_at(w - 1, 0, w, h) == Zone::TopRight);
  CHECK(WindowFrame::zone_at(0, h - 1, w, h) == Zone::BottomLeft);
  CHECK(WindowFrame::zone_at(w - 1, h - 1, w, h) == Zone::BottomRight);
  // The caption's buttons are ordinary clicks, and a corner reaches a little along each edge.
  CHECK(WindowFrame::zone_at(w - 10, 10, w, h) == Zone::Client);
  CHECK(WindowFrame::zone_at(w - 30, 10, w, h) == Zone::Client);
  CHECK(WindowFrame::zone_at(8, 1, w, h) == Zone::TopLeft);
}

void test_content_is_inside_the_frame() {
  const Rect with_menu = WindowFrame::content(300, 560, true), without = WindowFrame::content(300, 560, false);
  CHECK(with_menu.x == luna::frame_w && with_menu.right() == 300 - luna::frame_w);
  CHECK(with_menu.y == luna::caption_h + luna::menu_h && with_menu.bottom() == 560 - luna::frame_w);
  CHECK(without.y == luna::caption_h);
}

void test_each_screen_has_its_window_size_and_cloud_has_one() {
  UiHarness h;
  CHECK(h.start(width, height));
  CHECK(!h.app.own_frame());
  CHECK(h.app.screen_size().key.empty());
  h.app.set_theme("messenger");
  CHECK(h.app.own_frame());
  CHECK(!h.app.ui().animations);
  h.app.show_home();
  CHECK(h.app.screen_size().key == "Home" && h.app.screen_size().w == 300 && h.app.screen_size().h == 560);
  h.app.show_session("ABC234");
  CHECK(h.app.screen_size().key == "Room" && h.app.screen_size().w == 640 && h.app.screen_size().h == 500);
  h.app.settings().open(SettingsPage::Sound);
  CHECK(h.app.screen_size().key == "Options");
  h.app.settings().close();
  h.app.show_prompt({.valid = [](std::string_view) { return true; }});
  CHECK(h.app.screen_size().key == "Prompt");
  h.app.close_prompt();
  h.app.set_theme("cloud");
  CHECK(!h.app.own_frame());
  CHECK(h.app.screen_size().key.empty());
}

void test_home_lists_the_seats_and_goes_straight_to_a_public_room() {
  UiHarness h;
  start_luna(h);
  HomeScreen& home = h.app.home();
  std::string joined;
  int created = 0, found = 0;
  home.on_join = [&](std::string code) { joined = std::move(code); };
  home.on_create = [&] { ++created; };
  home.on_find_public = [&] { ++found; };
  CHECK(!h.drawn(home, HomeScreen::SeatBase));  // none: one line says so

  home.held_seats = [] { return std::vector<std::string>{"ABC234", "XYZ789"}; };
  home.held_seat_ms = []() -> std::optional<std::uint64_t> { return 60000; };
  h.render();
  CHECK(h.click(home, HomeScreen::SeatBase + 1));
  CHECK(joined == "XYZ789");

  CHECK(h.click(home, HomeScreen::Find));  // Cloud asks here, because it lists no Seats
  CHECK(found == 1);
  CHECK(!home.asking_return());
  CHECK(h.click(home, HomeScreen::Create));
  CHECK(created == 1);

  joined.clear();
  CHECK(h.click(home, HomeScreen::Code));
  for (const wchar_t c : std::wstring_view(L"abc234")) h.app.char_input(c, false);
  CHECK(h.click(home, HomeScreen::Join));
  CHECK(joined == "ABC234");
}

void test_menus_do_what_the_shell_and_screens_do() {
  UiHarness h;
  start_luna(h);
  WindowFrame& frame = h.app.frame();
  int changelog = 0, exits = 0, minimized = 0, closed = 0;
  h.app.settings().on_version = [&] { ++changelog; };
  frame.on_exit = [&] { ++exits; };
  frame.on_minimize = [&] { ++minimized; };
  frame.on_close = [&] { ++closed; };
  const auto item = [](int menu, int index) { return WindowFrame::ItemBase + menu * 10 + index; };

  CHECK(h.click(frame, WindowFrame::Minimize));
  CHECK(h.click(frame, WindowFrame::Close));
  CHECK(minimized == 1 && closed == 1);

  CHECK(h.click(frame, WindowFrame::MenuBase));
  CHECK(frame.menu_open());
  h.key(VK_ESCAPE);
  CHECK(!frame.menu_open());

  // At Home, File holds only the way out.
  CHECK(h.click(frame, WindowFrame::MenuBase));
  CHECK(h.click(frame, item(0, 0)));
  CHECK(exits == 1 && !frame.menu_open());
  CHECK(h.click(frame, WindowFrame::MenuBase + 2));
  CHECK(h.click(frame, item(2, 0)));
  CHECK(changelog == 1);

  CHECK(h.click(frame, WindowFrame::MenuBase + 1));
  CHECK(h.click(frame, item(1, 0)));
  CHECK(h.app.settings().is_open());
  CHECK(!h.drawn(frame, WindowFrame::MenuBase));  // the options window has no menus
  CHECK(h.click(h.app.settings(), SettingsPanel::Droplet));
  CHECK(!h.app.settings().is_open());

  // In a Room, File also leaves it, asking as the Room's own leave button does.
  room_with_chat(h);
  CHECK(h.click(frame, WindowFrame::MenuBase));
  CHECK(h.click(frame, item(0, 0)));
  CHECK(h.drawn(h.app.dialogs(), Dialogs::Confirm));
  // A question holds the menus off, and leaves the caption's buttons alone.
  CHECK(h.click(frame, WindowFrame::MenuBase));
  CHECK(!frame.menu_open());
  CHECK(h.click(frame, WindowFrame::Minimize));
  CHECK(minimized == 2);
}

void test_only_links_show_the_hand() {
  UiHarness h;
  start_luna(h);
  const auto cursor_on = [&](const Surface& owner, int id) {
    const Ui::Hit* hit = h.app.ui().find_hit(&owner, id);
    CHECK(hit != nullptr);
    h.app.mouse_move(hit->rect.x + hit->rect.w / 2, hit->rect.y + hit->rect.h / 2);
    return h.app.cursor();
  };
  CHECK(cursor_on(h.app.home(), HomeScreen::Join) == Cursor::Arrow);
  CHECK(cursor_on(h.app.home(), HomeScreen::Create) == Cursor::Hand);
  CHECK(cursor_on(h.app.home(), HomeScreen::Code) == Cursor::IBeam);
}

void test_settings_flow_and_scroll() {
  UiHarness h;
  start_luna(h);
  SettingsPanel& settings = h.app.settings();
  Prefs& prefs = *h.app.ui().prefs;
  settings.open(SettingsPage::Sound);
  h.render();
  SoundTab& sound = settings.sound();
  CHECK(sound.content_height() > 0);

  const bool spatial = prefs.get(pref::spatial_audio);
  CHECK(h.click(sound, SoundTab::Spatial));
  CHECK(prefs.get(pref::spatial_audio) != spatial);
  CHECK(!h.drawn(sound, SoundTab::Balance) || prefs.get(pref::spatial_audio));

  // A click in the middle of the slider means about the middle of its range.
  CHECK(h.click(sound, SoundTab::SpeakerVolume));
  CHECK(prefs.get(pref::volume) >= 45 && prefs.get(pref::volume) <= 55);

  // The tab's rows are in a window too small for them, so a scrollbar works it.
  CHECK(h.drawn(settings, SettingsPanel::Thumb));
  const Ui::Hit* track = h.app.ui().find_hit(&settings, SettingsPanel::Track);
  CHECK(track != nullptr);
  const float x = track->rect.x + 2, bottom = track->rect.bottom() - 2;
  h.app.mouse_move(x, bottom);
  h.app.mouse_down(x, bottom, false);
  h.app.mouse_up();
  h.render();
  CHECK(settings.scroll_offset() == 20);
  CHECK(h.app.wheel(track->rect.x - 100, track->rect.y + 50, -1));
  h.render();
  CHECK(settings.scroll_offset() > 20);

  for (const SettingsPage page : {SettingsPage::Display, SettingsPage::Notify, SettingsPage::System, SettingsPage::Account}) {
    CHECK(h.click(settings, SettingsPanel::TabBase + static_cast<int>(page)));
    CHECK(settings.page() == page);
    CHECK(settings.scroll_offset() == 0);
  }
  CHECK(h.click(settings, SettingsPanel::TabBase + static_cast<int>(SettingsPage::Display)));
  // A list in the small window has room to open, and picks like any other.
  CHECK(h.click(settings.display(), DisplayTab::ThemeList));
  CHECK(h.click(settings.display(), DisplayTab::ThemeList + 2));
  CHECK(prefs.get(pref::theme) == all_theme_entries()[1]->id);
}

void test_room_controls() {
  UiHarness h;
  start_luna(h);
  room_with_chat(h);
  RoomScreen& room = h.app.room();
  Prefs& prefs = *h.app.ui().prefs;

  const bool muted = prefs.get(pref::mute_others);
  CHECK(h.click(room, RoomScreen::Mute));
  CHECK(prefs.get(pref::mute_others) != muted);
  CHECK(h.click(room, RoomScreen::Mute));

  CHECK(!h.drawn(room, RoomScreen::Leave));  // the File menu has it
  CHECK(h.click(room, RoomScreen::More));
  prefs.set(pref::public_notice_seen, true);
  int publics = 0;
  room.on_set_public = [&](bool) { ++publics; };
  CHECK(h.click(room, RoomScreen::OptPublic));
  CHECK(publics == 1);
  std::string copied;
  room.on_copy = [&](std::string code) {
    copied = std::move(code);
    return true;
  };
  CHECK(h.click(room, RoomScreen::OptCode));
  CHECK(copied == "ABC234");

  CHECK(h.drawn(room, RoomScreen::MemberBase + 1));
}

void test_someone_typing_redraws_only_their_picture() {
  UiHarness h;
  start_luna(h);
  room_with_chat(h);
  RoomScreen& room = h.app.room();
  h.app.tick(clock_ms());
  h.render();
  room.pulse(1);
  CHECK(room.speaking_level(1) == 1);
  CHECK(!h.app.animating());  // it comes and goes at once, with no frames between
  const auto area = h.app.only_moving_area();
  CHECK(area.has_value());
  CHECK(area->w == 42 && area->h == 42);  // the picture and its orange
}

void test_chat_sends_and_scrolls() {
  UiHarness h;
  start_luna(h);
  room_with_chat(h);
  ChatPanel& chat = h.app.room().chat();
  std::string sent;
  chat.on_send = [&](std::string text) {
    sent = std::move(text);
    return true;
  };
  CHECK(h.click(chat, ChatPanel::Input));
  for (const wchar_t c : std::wstring_view(L"hi")) h.app.char_input(c, false);
  h.render();
  CHECK(h.click(chat, ChatPanel::Send));
  CHECK(sent == "hi");
  CHECK(chat.input().text().empty());

  for (int i = 0; i < 60; ++i) chat.add({1, "c1", "너", "", "메시지 " + std::to_string(i), 1700000000000 + i * 600000});
  h.render();
  const float end = chat.scroll_offset();
  CHECK(end > 0);  // stays with the newest
  CHECK(h.click(chat, ChatPanel::BarUp));
  CHECK(chat.scroll_offset() == end - 20);
  CHECK(h.click(chat, ChatPanel::BarDown));
  CHECK(chat.scroll_offset() == end);
  chat.add({1, "c1", "너", "", "새 메시지", 1700000000000 + 61 * 600000});
  h.render();
  CHECK(chat.scroll_offset() > end);
}

void test_prompt_submits() {
  UiHarness h;
  start_luna(h);
  PromptScreen& prompt = h.app.prompt();
  std::string submitted;
  int backs = 0;
  prompt.on_submit = [&](std::string value) { submitted = std::move(value); };
  prompt.on_back = [&] { ++backs; };
  h.app.show_prompt({.heading = "Heading", .label = "Label", .hint = "Hint", .password = true, .cancelable = true, .valid = [](std::string_view v) { return v.size() >= 2; }});
  h.render();
  CHECK(h.click(prompt, PromptScreen::Submit));
  CHECK(submitted.empty());
  CHECK(h.click(prompt, PromptScreen::Field));
  for (const wchar_t c : std::wstring_view(L"ab")) h.app.char_input(c, false);
  h.render();
  CHECK(h.click(prompt, PromptScreen::Eye));
  CHECK(prompt.password_shown());
  CHECK(h.click(prompt, PromptScreen::Submit));
  CHECK(submitted == "ab");
  CHECK(h.click(prompt, PromptScreen::Back));
  CHECK(backs == 1);
}

void test_graphics_can_all_be_released() {
  UiHarness h;
  start_luna(h);
  room_with_chat(h);
  h.app.room().chat().add({1, "c1", "너", "", "안녕", 1700000000000});
  h.render();
  h.drop_target();
  ID2D1Factory* const factory = h.app.factory();
  factory->AddRef();
  h.app.release_graphics();
  CHECK(factory->Release() == 0);
  CHECK(h.make_target());
  CHECK(h.render());
}

float luminance(Color c) {
  const auto linear = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
  return 0.2126f * linear(c.r) + 0.7152f * linear(c.g) + 0.0722f * linear(c.b);
}

float contrast(Color a, Color b) {
  const float la = luminance(a), lb = luminance(b);
  return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f);
}

void test_the_palettes_can_be_read() {
  for (const ThemeEntry& entry : all_themes()) {
    if (entry.theme.skin != SkinId::Luna) continue;
    const LunaColors& l = entry.theme.luna;
    CHECK(contrast(l.ink, l.face) >= 7);
    CHECK(contrast(l.ink, l.field) >= 7);
    CHECK(contrast(l.ink_soft, l.face) >= 4.5f);
    CHECK(contrast(l.link, l.field) >= 4.5f);
    CHECK(contrast(l.legend, l.face) >= 4.5f);
    CHECK(contrast(l.select_ink, l.select) >= 4.5f);
    CHECK(contrast(l.tooltip_ink, l.tooltip) >= 7);
    CHECK(contrast(l.caption_ink, l.caption[2].color) >= 4.5f);
    CHECK(contrast(entry.theme.error_text, l.face) >= 3);
  }
}

void test_themes_are_listed_by_lightness_with_their_skin() {
  const auto id_in = [](const std::vector<const ThemeEntry*>& list, std::string_view id) {
    return std::ranges::any_of(list, [&](const ThemeEntry* entry) { return entry->id == id; });
  };
  CHECK(id_in(themes_of(false), "messenger") && !id_in(themes_of(true), "messenger"));
  CHECK(id_in(themes_of(true), "messengerdark") && !id_in(themes_of(false), "messengerdark"));
  CHECK(theme_named("messengerdark").theme.dark);
  CHECK(theme_named("messengerdark").theme.skin == SkinId::Luna);
  // Each has a label of its own.
  for (const ThemeEntry& entry : all_themes()) {
    CHECK(tr().*entry.label != nullptr);
    CHECK(std::ranges::count(all_themes(), entry.id, &ThemeEntry::id) == 1);
  }

  // Following the OS may pair Skins, and the frame changes with the OS.
  MemoryPrefs prefs;
  prefs.set(pref::system_theme, true);
  prefs.set(pref::light_theme, std::string_view("messenger"));
  prefs.set(pref::dark_theme, std::string_view("darkcloud"));
  CHECK(effective_theme(prefs, false).id == "messenger");
  CHECK(effective_theme(prefs, true).id == "darkcloud");
  UiHarness h;
  CHECK(h.start(width, height));
  h.app.set_theme(effective_theme(prefs, false).id);
  CHECK(h.app.own_frame());
  CHECK(!h.app.ui().animations);
  CHECK(h.render());
  h.app.set_theme(effective_theme(prefs, true).id);
  CHECK(!h.app.own_frame());
  CHECK(h.render());
  h.app.set_theme("messengerdark");
  CHECK(h.app.own_frame());
  CHECK(h.render());
}

void test_changing_skin_measures_the_chat_again() {
  UiHarness h;
  start_luna(h);
  room_with_chat(h);
  ChatPanel& chat = h.app.room().chat();
  for (int i = 0; i < 60; ++i) chat.add({1, "c1", "너", "", "메시지 " + std::to_string(i), 1700000000000 + i * 600000});
  h.render();
  const float luna_end = chat.scroll_offset();
  h.app.set_theme("cloud");
  h.render();
  CHECK(chat.scroll_offset() != luna_end);
  CHECK(h.render());
}

}  // namespace

int main() {
  test_points_of_the_frame_go_to_the_system();
  test_content_is_inside_the_frame();
  test_each_screen_has_its_window_size_and_cloud_has_one();
  test_home_lists_the_seats_and_goes_straight_to_a_public_room();
  test_menus_do_what_the_shell_and_screens_do();
  test_only_links_show_the_hand();
  test_settings_flow_and_scroll();
  test_room_controls();
  test_someone_typing_redraws_only_their_picture();
  test_chat_sends_and_scrolls();
  test_prompt_submits();
  test_graphics_can_all_be_released();
  test_changing_skin_measures_the_chat_again();
  test_the_palettes_can_be_read();
  test_themes_are_listed_by_lightness_with_their_skin();
  return crpp_test_result();
}
