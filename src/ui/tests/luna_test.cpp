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
  CHECK(h.app.screen_size().key == "Room");  // options have a window of their own
  h.app.settings().close();
  h.app.show_prompt({.valid = [](std::string_view) { return true; }});
  CHECK(h.app.screen_size().key == "Prompt");
  h.app.close_prompt();
  h.app.set_theme("cloud");
  CHECK(!h.app.own_frame());
  CHECK(h.app.screen_size().key.empty());
}

void test_equal_sizes_land_on_equal_whole_pixels() {
  UiHarness h;
  start_luna(h);
  for (const float dpi : {96.0f, 120.0f, 144.0f, 168.0f, 192.0f}) {
    const auto target = h.target_at(dpi);
    Painter p(h.app.ui().gfx, target.get());
    const float scale = dpi / 96;
    const auto whole = [&](float dips) { return std::abs(dips * scale - std::round(dips * scale)) < 0.01f; };
    CHECK(whole(p.line()) && p.line() * scale >= 1);
    const float width = std::round(21 * scale);
    for (float x = 0; x < 40; x += 0.25f) {
      const Rect r = p.snap({x, x / 2, 21, 21});
      CHECK(whole(r.x) && whole(r.y));
      CHECK(std::abs(r.w * scale - width) < 0.01f && std::abs(r.h * scale - width) < 0.01f);
      const Rect again = p.snap(r);  // lines counted from a snapped box stay on it
      CHECK(std::abs(again.x - r.x) < 0.001f && std::abs(again.w - r.w) < 0.001f);
    }
    CHECK(p.snap({5, 5, 0.5f, 0}).w * scale >= 0.99f);  // a thin line never vanishes
    CHECK(p.snap({5, 5, 0.5f, 0}).h == 0);
  }
  // Cloud is drawn where it is asked to be.
  h.app.set_theme("cloud");
  const auto target = h.target_at(120);
  Painter p(h.app.ui().gfx, target.get());
  const Rect r = p.snap({1.3f, 2.7f, 21, 21});
  CHECK(r.x == 1.3f && r.y == 2.7f && r.w == 21);
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
  CHECK(h.drawn(frame, WindowFrame::MenuBase));  // the main window goes on as it was
  CHECK(h.app.take_raised_pane() == Pane::Options);
  h.on_pane(Pane::Options);
  CHECK(h.click(h.app.settings(), SettingsPanel::Droplet));
  CHECK(!h.app.settings().is_open());
  h.on_pane(Pane::Main);

  // In a Room, File also leaves it, asking as the Room's own leave button does.
  room_with_chat(h);
  CHECK(h.click(frame, WindowFrame::MenuBase));
  CHECK(h.click(frame, item(0, 0)));
  CHECK(h.app.dialogs().is_open());
  h.on_pane(Pane::Dialog);
  CHECK(h.drawn(h.app.dialogs(), Dialogs::Confirm));
  h.key(VK_ESCAPE);
  CHECK(!h.app.dialogs().is_open());
  h.on_pane(Pane::Main);
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
  h.on_pane(Pane::Options);
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

void test_panels_have_windows_of_their_own() {
  UiHarness h;
  CHECK(h.start(width, height));
  // Under Cloud they are layers of the only window.
  h.app.settings().open(SettingsPage::Sound);
  h.app.dialogs().show({.text = "?", .confirm = "OK"});
  h.render();
  CHECK(h.app.open_panes().empty());
  CHECK(h.drawn(h.app.dialogs(), Dialogs::Confirm));
  h.app.dialogs().close();
  h.app.settings().close();

  start_luna(h);
  CHECK(h.app.open_panes().empty());
  h.app.settings().open(SettingsPage::System);
  h.app.changelog().open({}, 0);
  h.app.dialogs().show({.text = "?", .confirm = "OK", .cancel = "No"});
  h.app.offline().show();
  h.render();
  CHECK((h.app.open_panes() == std::vector<Pane>{Pane::Options, Pane::Changelog, Pane::Offline, Pane::Dialog}));
  // None of them is drawn on the main window, whose menus still work beside a lost server.
  CHECK(!h.drawn(h.app.settings(), SettingsPanel::Overlay) && !h.drawn(h.app.dialogs(), Dialogs::Confirm));
  CHECK(!h.drawn(h.app.changelog(), ChangelogView::Card));
  CHECK(h.drawn(h.app.frame(), WindowFrame::MenuBase));
  CHECK(h.drawn(h.app.offline(), OfflinePopup::Backdrop));

  // A window's size comes from what it holds, or was left to the user.
  const PaneInfo options = h.app.pane_info(Pane::Options), dialog = h.app.pane_info(Pane::Dialog), offline = h.app.pane_info(Pane::Offline);
  CHECK(options.resizable && options.key == "Options" && options.w == 580);
  CHECK(!dialog.resizable && dialog.close && dialog.w > 100 && dialog.h > luna::caption_h);
  CHECK(!offline.close);  // it goes when the server is back
  const PaneState before = h.app.pane_state();
  h.app.dialogs().show({.text = "One\nTwo\nThree", .confirm = "OK"});
  CHECK(h.app.pane_state() != before);
  CHECK(!h.app.pane_info(Pane::Dialog).close && h.app.pane_info(Pane::Dialog).h > dialog.h);

  // Enter answers the question in its window, Escape only where there is a way to say no.
  bool yes = false;
  h.app.dialogs().show({.text = "?", .confirm = "OK", .on_confirm = [&] { yes = true; }});
  h.on_pane(Pane::Dialog);
  CHECK(h.drawn(h.app.dialogs(), Dialogs::Confirm));
  h.key(VK_ESCAPE);
  CHECK(h.app.dialogs().is_open());
  h.key(VK_RETURN);
  CHECK(yes && !h.app.dialogs().is_open());

  // Each window has its own hits, hover and keyboard field.
  h.on_pane(Pane::Main);
  HomeScreen& home = h.app.home();
  h.app.offline().hide();
  h.render();
  CHECK(h.click(home, HomeScreen::Code));
  const Ui::Hit* join = h.app.ui().find_hit(&home, HomeScreen::Join);
  CHECK(join != nullptr);
  h.app.mouse_move(join->rect.x + 1, join->rect.y + 1);
  CHECK(h.app.ui().hot(&home, HomeScreen::Join));
  CHECK(h.app.wants_blink());
  h.on_pane(Pane::Options);
  CHECK(h.drawn(h.app.settings(), SettingsPanel::Droplet) && !h.drawn(home, HomeScreen::Join));
  CHECK(!h.app.ui().hot(&home, HomeScreen::Join) && !h.app.wants_blink());
  CHECK(h.app.ui().window == static_cast<int>(Pane::Options));
  {
    const PaneScope back(h.app, Pane::Main);
    CHECK(h.app.ui().hot(&home, HomeScreen::Join) && h.app.wants_blink());
    h.app.ui().toasts->toast("from the main window");
  }
  CHECK(h.app.current_pane() == Pane::Options);
  CHECK(h.app.toasts().shown_in() == 0);
  h.app.toasts().toast("from here");  // the main window may be behind this one
  CHECK(h.app.toasts().shown_in() == static_cast<int>(Pane::Options));

  // What runs on time runs whichever window is in front.
  int retried = 0;
  h.app.offline().show();
  h.app.offline().on_retry = [&] { ++retried; };
  h.app.offline().retry_at(5);
  CHECK(h.app.wake_at() == 5);
  (void)h.app.tick(10);
  CHECK(retried == 1);
  h.app.offline().hide();

  // A window closes by its own button, and a new start forgets where the pointer was.
  h.render();
  CHECK(h.drawn(h.app.frame(), WindowFrame::Close) == false);
  h.app.mouse_move(1, 1);
  h.app.close_pane(Pane::Options);
  CHECK(!h.app.settings().is_open());
  h.app.forget_pane(Pane::Options);
  CHECK(h.app.ui().window == static_cast<int>(Pane::Options) && h.app.ui().hover_owner == nullptr);
  h.on_pane(Pane::Changelog);
  CHECK(h.drawn(h.app.changelog(), ChangelogView::Card));
  CHECK(!h.drawn(h.app.changelog(), ChangelogView::Close));
  h.key(VK_ESCAPE);
  CHECK(!h.app.changelog().is_open());

  // The picker closes when the keyboard leaves, as menus do on the main window.
  h.on_pane(Pane::Main);
  h.app.emoji_picker().show("", nullptr);
  CHECK(h.app.open_panes() == std::vector<Pane>{Pane::Emoji});
  h.on_pane(Pane::Emoji);
  CHECK(h.app.window_deactivated());
  CHECK(!h.app.emoji_picker().is_open());
  h.on_pane(Pane::Main);
  CHECK(h.click(h.app.frame(), WindowFrame::MenuBase));
  CHECK(h.app.frame().menu_open());
  (void)h.app.window_deactivated();
  CHECK(!h.app.frame().menu_open());

  // A dialog cannot be dragged by what is not its caption, nor resized.
  using Zone = WindowFrame::Zone;
  const WindowFrame::Parts fixed{.resizable = false, .minimize = false};
  CHECK(WindowFrame::zone_at(0, 100, 300, 200, fixed) == Zone::Client);
  CHECK(WindowFrame::zone_at(100, 0, 300, 200, fixed) == Zone::Caption);
  CHECK(WindowFrame::zone_at(280, 12, 300, 200, fixed) == Zone::Client);  // its close button
  CHECK(WindowFrame::zone_at(280, 12, 300, 200, {.resizable = false, .minimize = false, .close = false}) == Zone::Caption);
  // Graphics go, and come back for any window.
  h.drop_target();
  h.app.release_graphics();
  CHECK(h.make_target());
  h.app.settings().open(SettingsPage::Sound);
  h.on_pane(Pane::Options);
  CHECK(h.render());
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
  test_equal_sizes_land_on_equal_whole_pixels();
  test_home_lists_the_seats_and_goes_straight_to_a_public_room();
  test_menus_do_what_the_shell_and_screens_do();
  test_only_links_show_the_hand();
  test_settings_flow_and_scroll();
  test_panels_have_windows_of_their_own();
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
