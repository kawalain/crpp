// Cloud drawn from its pack in skins/: in a window that scales the stage, the same places to click as
// the C++ layout, and motion that the pack states and that ends. Pixels are compared with tests/shots.
#include <cmath>
#include <string>
#include <vector>

#include "check.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
using test::UiHarness;

namespace {

constexpr const char* cloud_pack = CRPP_TEST_SKINS "/cloud";

struct Place {
  const Surface* owner;
  int id;
};

std::vector<Rect> rects_of(UiHarness& h, const std::vector<Place>& places) {
  h.render();
  std::vector<Rect> found;
  for (const Place& place : places) {
    const Ui::Hit* hit = h.app.ui().find_hit(place.owner, place.id);
    CHECK(hit != nullptr);
    found.push_back(hit ? hit->rect : Rect{});
  }
  return found;
}

void same_places(UiHarness& h, const std::vector<Place>& places) {
  const std::vector<Rect> code = rects_of(h, places);
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  CHECK(h.app.ui().palette >= 0);
  const std::vector<Rect> pack = rects_of(h, places);
  for (std::size_t i = 0; i < places.size(); ++i) {
    CHECK(std::abs(pack[i].x - code[i].x) < 0.01f && std::abs(pack[i].y - code[i].y) < 0.01f);
    CHECK(std::abs(pack[i].w - code[i].w) < 0.01f && std::abs(pack[i].h - code[i].h) < 0.01f);
  }
}

void test_the_layers_stand_where_the_code_puts_them() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_home();
  h.app.toasts().toast("A message", "Do it", [] {});
  h.app.update_popup().show_available();
  h.app.notice().set(Notice{"A longer notice that takes more than one line of this window to say what it wants to say", "Act", "Later"});
  h.app.dialogs().show({"Is this a question that wraps on to a second line of its own?", "Yes", "No", false, nullptr, nullptr});
  h.app.offline().show();
  const Surface* dialogs = &h.app.dialogs();
  const Surface* offline = &h.app.offline();
  const Surface* notice = &h.app.notice();
  const Surface* update = &h.app.update_popup();
  same_places(h, {{dialogs, Dialogs::Backdrop}, {dialogs, Dialogs::Card}, {dialogs, Dialogs::Confirm}, {dialogs, Dialogs::Cancel},
                  {offline, OfflinePopup::Backdrop}, {offline, OfflinePopup::Card},
                  {notice, NoticeCard::Card}, {notice, NoticeCard::Action}, {notice, NoticeCard::Dismiss},
                  {update, UpdatePopup::Card}, {update, UpdatePopup::Button},
                  {&h.app.toasts(), Toasts::Action}});
}

void test_the_prompt_stands_where_the_code_puts_it_and_clicks_land() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  PromptScreen& prompt = h.app.prompt();
  h.app.show_prompt({.heading = "Hello", .label = "Name", .placeholder = "name", .hint = "A hint", .value = "abc", .prefix = "@", .extra = "One", .extra2 = "Two", .link = "A link", .password = true, .cancelable = true, .valid = [](std::string_view v) { return !v.empty(); }});
  h.render();
  const Rect caret_of_code = h.app.ui().caret;
  same_places(h, {{&prompt, PromptScreen::Field}, {&prompt, PromptScreen::Submit}, {&prompt, PromptScreen::Eye}, {&prompt, PromptScreen::Back},
                  {&prompt, PromptScreen::Extra}, {&prompt, PromptScreen::Extra2}, {&prompt, PromptScreen::Link}});
  const Rect caret = h.app.ui().caret;
  CHECK(std::abs(caret.x - caret_of_code.x) < 0.01f && std::abs(caret.y - caret_of_code.y) < 0.01f);
  CHECK(!prompt.password_shown());
  CHECK(h.click(prompt, PromptScreen::Eye));
  CHECK(prompt.password_shown());
  std::string submitted;
  prompt.on_submit = [&](std::string value) { submitted = std::move(value); };
  CHECK(h.click(prompt, PromptScreen::Submit));
  CHECK(submitted == "abc");
}

void test_a_list_stands_where_the_code_puts_it() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_home();
  h.app.settings().open(SettingsPage::System);
  SystemTab& system = h.app.settings().system();
  system.click(SystemTab::IdleList, 0, 0, false);
  same_places(h, {{&system, SystemTab::IdleList + 1}, {&system, SystemTab::IdleList + 2}});
}

void test_the_rows_choose_a_skin_a_theme_and_a_dark_mode() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  App& app = h.app;
  CHECK(app.find_skins(CRPP_TEST_SKINS));
  CHECK(app.ui().skins.size() == 2);
  const Prefs& prefs = *app.ui().prefs;
  app.apply_skin_choice(false);
  CHECK(app.ui().palette >= 0);
  CHECK(app.ui().pack->id == "cloud");
  app.show_home();
  app.settings().open(SettingsPage::Display);
  DisplayTab& display = app.settings().display();
  h.render();

  // Dark on.
  CHECK(h.click(display, DisplayTab::DarkModeList));
  CHECK(h.click(display, DisplayTab::DarkModeList + 2));
  CHECK(prefs.get(pref::dark_mode) == "on");
  CHECK(prefs.get(pref::skin) == "cloud");
  app.apply_skin_choice(false);
  CHECK(app.ui().theme.dark);

  // 파스텔 has no dark variant: the row locks, and the choice stays for the next Theme.
  h.render();
  CHECK(h.click(display, DisplayTab::SkinThemeList));
  CHECK(h.click(display, DisplayTab::SkinThemeList + 2));
  CHECK(prefs.get(pref::skin_theme) == "pastel");
  app.apply_skin_choice(false);
  CHECK(!app.ui().theme.dark);
  h.render();
  (void)h.click(display, DisplayTab::DarkModeList);
  CHECK(!h.drawn(display, DisplayTab::DarkModeList + 1));
  CHECK(prefs.get(pref::dark_mode) == "on");

  // Another Skin goes to its default Theme, keeping the mode.
  CHECK(h.click(display, DisplayTab::SkinList));
  CHECK(h.click(display, DisplayTab::SkinList + 2));
  CHECK(prefs.get(pref::skin) == "retro");
  CHECK(prefs.get(pref::skin_theme) == "classic");
  app.apply_skin_choice(false);
  CHECK(app.ui().pack->id == "retro");
  CHECK(app.ui().theme.dark);
  CHECK(app.own_frame());
  h.render();

  // And back. Upstream's keys are never touched.
  app.settings().close();
  CHECK(prefs.get(pref::theme) == pref::theme.fallback);
}

// The clock is held, so that a frame's time is what the test says.
class Moving {
 public:
  explicit Moving(UiHarness& h) : h_(h) {
    h_.app.set_animations(true);
    at(1000);
  }
  ~Moving() { hold_clock_at(0); }
  Moving(const Moving&) = delete;
  Moving& operator=(const Moving&) = delete;
  // The time goes on by this much, and a frame is drawn.
  bool after(unsigned long long ms) {
    at(now_ + ms);
    (void)h_.app.tick(now_);
    h_.render();
    return h_.app.animating();
  }

 private:
  void at(unsigned long long ms) {
    now_ = ms;
    hold_clock_at(ms);
  }
  UiHarness& h_;
  unsigned long long now_ = 0;
};

void test_a_dialog_rises_and_then_asks_for_no_more_frames() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_home();
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  h.render();
  Moving moving(h);
  h.app.dialogs().show({"Sure?", "Yes", "No", false, nullptr, nullptr});
  CHECK(moving.after(0));
  CHECK(moving.after(80));
  CHECK(!moving.after(80));
  CHECK(!moving.after(1000));
  h.app.dialogs().close();
}

void test_a_toast_comes_goes_and_is_gone() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_home();
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  h.render();
  Moving moving(h);
  bool acted = false;
  h.app.toasts().toast("A message", "Do it", [&] { acted = true; }, 1000);
  CHECK(moving.after(0));
  CHECK(h.drawn(h.app.toasts(), Toasts::Action));
  CHECK(moving.after(90));
  CHECK(!moving.after(90));
  CHECK(!moving.after(400));
  CHECK(h.app.toasts().wake_at() != 0);
  CHECK(moving.after(420));  // past its time, it slides away
  CHECK(h.drawn(h.app.toasts(), Toasts::Action));
  CHECK(moving.after(90));
  CHECK(!moving.after(90));
  CHECK(!h.drawn(h.app.toasts(), Toasts::Action));
  CHECK(h.app.toasts().wake_at() == 0);
  CHECK(!acted);
}

void test_a_tip_fades_out_before_it_lets_go_of_its_text() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_session("ABC234");
  h.app.room().set_room({{0, "Me", "", false, "c0"}, {1, "You", "", false, "c1"}}, 0, 0, false, true);
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  h.render();
  RoomScreen& room = h.app.room();
  Moving moving(h);
  h.app.ui().hover_owner = &room;
  h.app.ui().hover_id = RoomScreen::Leave;
  CHECK(moving.after(0));
  CHECK(room.hover_tip().visible());
  CHECK(!moving.after(150));
  const std::string words = room.hover_tip().text();
  CHECK(!words.empty());
  h.app.ui().hover_owner = nullptr;
  h.app.ui().hover_id = 0;
  CHECK(moving.after(0));
  CHECK(!room.hover_tip().visible());
  CHECK(room.hover_tip().text() == words);
  (void)moving.after(150);
  CHECK(room.hover_tip().text().empty());
  CHECK(!moving.after(1000));
}

void test_the_offline_popup_rises_once() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_home();
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  h.render();
  Moving moving(h);
  h.app.offline().show();
  CHECK(moving.after(0));
  CHECK(!moving.after(160));
  CHECK(!moving.after(0));  // it is on screen and settled, and walks on without a transition
  h.app.offline().hide();
  (void)moving.after(0);
  h.app.offline().show();
  CHECK(moving.after(0));  // shown anew, it rises anew
}

void test_every_tab_stands_where_the_code_puts_it() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_home();
  SettingsPanel& panel = h.app.settings();
  panel.set_version("v1.2.3");
  panel.notify().set_sounds({{"a", "a", "One"}, {"b", "b", "Two"}});
  panel.account().set_profile({"Me", "me", "Me", "", true});
  panel.open(SettingsPage::Sound);
  const Surface* panel_hits = &panel;
  std::vector<Place> places = {{panel_hits, SettingsPanel::Droplet}, {panel_hits, SettingsPanel::Overlay}, {panel_hits, SettingsPanel::Version}, {panel_hits, SettingsPanel::Track}, {panel_hits, SettingsPanel::Thumb}};
  for (int i = 0; i < 5; ++i) places.push_back({panel_hits, SettingsPanel::TabBase + i});
  const Surface* sound = &panel.sound();
  for (const int id : {SoundTab::SpeakerVolume, SoundTab::KeyboardVolume, SoundTab::MouseVolume, SoundTab::SpeakerList, SoundTab::KeyboardList, SoundTab::MouseList, SoundTab::ListenSelf, SoundTab::OthersUseMine}) places.push_back({sound, id});
  same_places(h, places);

  const auto compare = [](SettingsPage page, const std::vector<int>& ids) {
    UiHarness other;
    CHECK(other.start(1200, 900));
    other.app.show_home();
    SettingsPanel& panel = other.app.settings();
    panel.notify().set_sounds({{"a", "a", "One"}, {"b", "b", "Two"}});
    panel.account().set_profile({"Me", "me", "Me", "", true});
    panel.open(page);
    const Surface* owner = page == SettingsPage::Display   ? static_cast<const Surface*>(&panel.display())
                           : page == SettingsPage::Notify ? static_cast<const Surface*>(&panel.notify())
                           : page == SettingsPage::System ? static_cast<const Surface*>(&panel.system())
                                                          : static_cast<const Surface*>(&panel.account());
    std::vector<Place> list;
    for (const int id : ids) list.push_back({owner, id});
    same_places(other, list);
  };
  compare(SettingsPage::Notify, {NotifyTab::TrayToggle, NotifyTab::FlashToggle, NotifyTab::SoundBase, NotifyTab::SoundBase + 1, NotifyTab::PreviewBase, NotifyTab::PreviewBase + 1});
  compare(SettingsPage::System, {SystemTab::Autostart, SystemTab::Tray, SystemTab::Detail, SystemTab::Use24h, SystemTab::IdleList});
  compare(SettingsPage::Account, {AccountTab::Emoji, AccountTab::Nickname, AccountTab::Handle, AccountTab::Logout});
}

void test_clicks_in_the_tabs_do_what_they_do_on_the_code() {
  // What a click at the middle of each control leaves, without a pack and with one.
  const auto after_clicks = [](bool pack) {
    UiHarness h;
    CHECK(h.start(1200, 900));
    h.app.show_home();
    if (pack) CHECK(h.app.load_skin_dir(cloud_pack).has_value());
    SettingsPanel& panel = h.app.settings();
    panel.open(SettingsPage::Sound);
    h.render();
    SoundTab& sound = panel.sound();
    CHECK(h.click(sound, SoundTab::KeyboardVolume));
    CHECK(h.click(sound, SoundTab::ListenSelf));
    panel.scroll_to(10000);
    h.render();
    CHECK(panel.scroll_offset() > 0);
    CHECK(h.click(sound, SoundTab::Balance));
    panel.open(SettingsPage::Display);
    h.render();
    CHECK(h.click(panel.display(), DisplayTab::ScaleSlider));
    const Prefs& prefs = *h.app.ui().prefs;
    return std::vector<int>{prefs.get(pref::keyboard_volume), prefs.get(pref::listen_self) ? 1 : 0, prefs.get(pref::channel_balance), prefs.get(pref::chat_scale)};
  };
  CHECK(after_clicks(false) == after_clicks(true));
}

void test_a_scroll_asked_before_the_first_frame_waits_for_the_height() {
  UiHarness h;
  CHECK(h.start(951, 692));
  h.app.show_home();
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  h.render();
  SettingsPanel& panel = h.app.settings();
  panel.open(SettingsPage::System);
  panel.scroll_to(10000);
  h.render();
  CHECK(panel.scroll_offset() > 0);
}

void test_the_panel_comes_and_goes_and_only_the_droplet_takes_clicks_as_it_goes() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_home();
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  h.render();
  SettingsPanel& panel = h.app.settings();
  Moving moving(h);
  CHECK(!moving.after(0));
  CHECK(h.drawn(panel, SettingsPanel::Droplet));
  CHECK(!h.drawn(panel, SettingsPanel::Overlay));
  panel.open(SettingsPage::Sound);
  CHECK(moving.after(0));
  CHECK(moving.after(80));
  CHECK(!moving.after(80));
  CHECK(h.drawn(panel, SettingsPanel::Overlay));
  CHECK(h.drawn(panel.sound(), SoundTab::ListenSelf));
  panel.close();
  CHECK(moving.after(0));
  CHECK(h.drawn(panel, SettingsPanel::Droplet));
  CHECK(!h.drawn(panel, SettingsPanel::Overlay));
  CHECK(!h.drawn(panel.sound(), SoundTab::ListenSelf));
  CHECK(moving.after(80));
  CHECK(!moving.after(80));
  CHECK(!moving.after(1000));
}

void test_a_change_of_tab_fades_and_ends() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_home();
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  SettingsPanel& panel = h.app.settings();
  panel.open(SettingsPage::Sound);
  h.render();
  Moving moving(h);
  (void)moving.after(1000);
  CHECK(!moving.after(1000));
  panel.select(SettingsPage::System);
  CHECK(moving.after(0));
  CHECK(h.drawn(panel.system(), SystemTab::Autostart));
  CHECK(!h.drawn(panel.sound(), SoundTab::ListenSelf));  // on its way out it takes no clicks
  CHECK(moving.after(70));
  CHECK(!moving.after(150));
  CHECK(!moving.after(1000));
}

void test_scrolling_settles_and_the_bar_goes_away() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_home();
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  SettingsPanel& panel = h.app.settings();
  panel.open(SettingsPage::Sound);
  h.render();
  Moving moving(h);
  (void)moving.after(1000);
  CHECK(!moving.after(1000));
  CHECK(!panel.scrollbar_shown());
  panel.wheel(0, 0, -1);
  CHECK(moving.after(16));
  CHECK(panel.scrollbar_shown());
  for (int i = 0; i < 100 && moving.after(16); ++i) {
  }
  CHECK(panel.scroll_offset() == 80);
  CHECK(!moving.after(0));
  CHECK(panel.wake_at() != 0);
  CHECK(moving.after(2000));  // its pause is over
  CHECK(!panel.scrollbar_shown());
  for (int i = 0; i < 100 && moving.after(16); ++i) {
  }
  CHECK(!moving.after(1000));
}

void enter(UiHarness& h, bool chat) {
  h.app.show_session("ABC234");
  h.app.room().set_room({{0, "Me", "", false, "c0"}, {1, "Other", "", false, "c1"}, {2, "Third", "", true, "c2"}}, 0, 0, false, chat);
}

void test_a_room_stands_where_the_code_puts_it() {
  for (const bool chat : {false, true}) {
    UiHarness h;
    CHECK(h.start(1200, 900));
    enter(h, chat);
    RoomScreen& room = h.app.room();
    room.chat().add({0, "c0", "Me", "", "Hello", 1700000000000});
    std::vector<Place> places = {{&room, RoomScreen::Leave}, {&room, RoomScreen::Mute}, {&room, RoomScreen::More}, {&room, RoomScreen::MemberBase}, {&room, RoomScreen::MemberBase + 1}, {&room, RoomScreen::MemberBase + 2}};
    if (chat) {
      const Surface* panel = &room.chat();
      places.insert(places.end(), {{panel, ChatPanel::Area}, {panel, ChatPanel::Input}, {panel, ChatPanel::Send}});
    }
    h.render();
    room.click(RoomScreen::More, 0, 0, false);
    for (const int id : {RoomScreen::Popup, RoomScreen::OptCode, RoomScreen::OptPublic, RoomScreen::OptChat}) places.push_back({&room, id});
    same_places(h, places);
  }
}

void test_the_volume_popup_stands_where_the_code_puts_it() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  enter(h, false);
  RoomScreen& room = h.app.room();
  h.render();
  CHECK(room.right_click(RoomScreen::Mute, 0, 0));
  same_places(h, {{&room, RoomScreen::Popup}, {&room, RoomScreen::VolumeSlider}});
}

void test_what_opens_from_a_room_stands_where_the_code_puts_it() {
  {
    UiHarness h;
    CHECK(h.start(1200, 900));
    enter(h, false);
    MemberMenu& menu = h.app.room().member_menu();
    menu.open(1, false, 70, 300, 320);
    same_places(h, {{&menu, MemberMenu::Card}, {&menu, MemberMenu::Profile}, {&menu, MemberMenu::Slider}, {&menu, MemberMenu::Mute}});
  }
  {
    UiHarness h;
    CHECK(h.start(1200, 900));
    enter(h, false);
    RoomScreen& room = h.app.room();
    room.profile_card().request(0);
    room.show_profile({0, "Me", "1f600", "me", 1700000000, 3900, 12345});
    const ProfileCard& card = room.profile_card();
    same_places(h, {{&card, ProfileCard::Backdrop}, {&card, ProfileCard::Card}, {&card, ProfileCard::Icon}});
  }
  {
    UiHarness h;
    CHECK(h.start(1200, 900));
    enter(h, false);
    EmojiPicker& picker = h.app.emoji_picker();
    picker.show("1f600", nullptr);
    same_places(h, {{&picker, EmojiPicker::Backdrop}, {&picker, EmojiPicker::Card}});
  }
  {
    UiHarness h;
    CHECK(h.start(1200, 900));
    h.app.show_home();
    ChangelogView& view = h.app.changelog();
    view.open({}, 0);
    same_places(h, {{&view, ChangelogView::Backdrop}, {&view, ChangelogView::Card}, {&view, ChangelogView::Close}});
    CHECK(h.click(view, ChangelogView::Close));
    CHECK(!view.is_open());
  }
}

void settled(UiHarness& h, Moving& moving) {
  for (int i = 0; i < 400 && moving.after(16); ++i) {
  }
  CHECK(!moving.after(0));
  CHECK(!h.app.ui().more_frames);
  CHECK(!moving.after(5000));
  CHECK(!h.app.ui().more_frames);
}

void room_from_the_pack(UiHarness& h, bool chat) {
  CHECK(h.start(1200, 900));
  enter(h, chat);
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  h.render();
}

void test_a_room_at_rest_asks_for_no_frames() {
  for (const bool chat : {false, true}) {
    UiHarness h;
    room_from_the_pack(h, chat);
    RoomScreen& room = h.app.room();
    if (chat) room.chat().add({1, "c1", "Other", "", "A message", 1700000000000});
    h.render();
    Moving moving(h);
    settled(h, moving);
    CHECK(!room.only_moving_area().has_value());
    h.app.settings().open(SettingsPage::Sound);
    settled(h, moving);
    h.app.settings().close();
    settled(h, moving);
  }
}

void test_a_disc_draws_only_itself_and_held_up_it_costs_nothing() {
  UiHarness h;
  room_from_the_pack(h, true);
  RoomScreen& room = h.app.room();
  Moving moving(h);
  settled(h, moving);
  room.pulse(1);
  CHECK(room.take_new_pulse());
  (void)moving.after(0);
  const auto area = h.app.only_moving_area();
  CHECK(area.has_value());
  const Ui::Hit* row = h.app.ui().find_hit(&room, RoomScreen::MemberBase + 1);
  CHECK(row != nullptr);
  if (area && row) {
    CHECK(std::abs(area->x - (row->rect.x + 6)) < 0.01f && std::abs(area->y - (row->rect.y + 1.5f)) < 0.01f);
    CHECK(area->w == 44 && area->h == 44);
  }
  CHECK(moving.after(50));
  // At full strength it asks for nothing, however often it is renewed.
  CHECK(!moving.after(50));
  room.pulse(1);
  CHECK(!room.take_new_pulse());
  CHECK(!room.animating());
  CHECK(!h.app.ui().more_frames);
  // Something else moving wants the whole window.
  room.chat().blocked(5000);
  (void)moving.after(0);
  CHECK(h.app.ui().more_frames);
  CHECK(!h.app.only_moving_area().has_value());
}

void test_someone_arrives_once_and_a_first_list_does_not() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  Moving moving(h);
  h.app.show_session("ABC234");
  RoomScreen& room = h.app.room();
  room.set_room({{0, "Me", "", false, "c0"}, {1, "Other", "", false, "c1"}}, 0, 0, false, false);
  settled(h, moving);
  const std::vector<MemberView> three = {{0, "Me", "", false, "c0"}, {1, "Other", "", false, "c1"}, {2, "Third", "", false, "c2"}};
  room.set_room(three, 0, 0, false, false);
  CHECK(moving.after(0));
  const Ui::Hit* hit = h.app.ui().find_hit(&room, RoomScreen::MemberBase + 2);
  CHECK(hit != nullptr);
  const float risen_from = hit ? hit->rect.y : 0;
  CHECK(moving.after(150));
  settled(h, moving);
  hit = h.app.ui().find_hit(&room, RoomScreen::MemberBase + 2);
  CHECK(hit != nullptr);
  if (hit) CHECK(std::abs(risen_from - hit->rect.y - 8) < 2);
  // Someone leaving moves them up the list, which is no arrival.
  room.set_room({{0, "Me", "", false, "c0"}, {2, "Third", "", false, "c2"}}, 0, 0, false, false);
  CHECK(!moving.after(0));
  CHECK(!h.app.ui().more_frames);
}

void test_a_message_rises_and_ends() {
  UiHarness h;
  room_from_the_pack(h, true);
  Moving moving(h);
  ChatPanel& chat = h.app.room().chat();
  chat.add({1, "c1", "Other", "", "First", 1700000000000});
  settled(h, moving);
  chat.add({1, "c1", "Other", "", "Second", 1700000060000});
  CHECK(moving.after(0));
  CHECK(moving.after(150));
  settled(h, moving);
  CHECK(chat.shown() == 2);
}

void test_quieting_everyone_fades_and_ends() {
  UiHarness h;
  room_from_the_pack(h, false);
  Moving moving(h);
  settled(h, moving);
  h.app.ui().prefs->set(pref::mute_others, true);
  CHECK(moving.after(0));
  CHECK(moving.after(100));
  CHECK(!moving.after(100));
  settled(h, moving);
}

void test_the_chat_shakes_calms_and_ends() {
  UiHarness h;
  room_from_the_pack(h, true);
  Moving moving(h);
  settled(h, moving);
  ChatPanel& chat = h.app.room().chat();
  chat.blocked(5000);
  CHECK(moving.after(0));
  CHECK(moving.after(300));
  CHECK(!moving.after(200));  // shaken, and the bubble is up
  CHECK(!h.app.ui().more_frames);
  CHECK(moving.after(10000));  // the wait is over and the bubble goes
  settled(h, moving);
  CHECK(!chat.limiter().calm());
}

void test_the_send_button_moves_out_for_a_second_line() {
  UiHarness h;
  room_from_the_pack(h, true);
  Moving moving(h);
  settled(h, moving);
  ChatPanel& chat = h.app.room().chat();
  const Ui::Hit* send = h.app.ui().find_hit(&chat, ChatPanel::Send);
  CHECK(send != nullptr);
  const float y = send ? send->rect.y : 0;
  chat.input().set_text(L"one\ntwo");
  CHECK(moving.after(0));
  settled(h, moving);
  send = h.app.ui().find_hit(&chat, ChatPanel::Send);
  CHECK(send != nullptr);
  if (send) CHECK(send->rect.y > y + 40);
}

void test_a_refused_switch_shakes_its_tip_longer_than_the_tip_fades() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_session("ABC234");
  RoomScreen& room = h.app.room();
  room.set_room({{0, "Me", "", false, "c0"}, {1, "Other", "", false, "c1"}}, 0, 0, true, false);
  h.app.ui().prefs->set(pref::pin_public, true);
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  h.render();
  Moving moving(h);
  settled(h, moving);
  CHECK(h.click(room, RoomScreen::More));
  settled(h, moving);
  const Ui::Hit* hit = h.app.ui().find_hit(&room, RoomScreen::OptPublic);
  CHECK(hit != nullptr);
  if (hit) h.app.mouse_move(hit->rect.x + 1, hit->rect.y + 1);
  CHECK(moving.after(0));  // its tip comes up
  settled(h, moving);
  CHECK(h.click(room, RoomScreen::OptPublic));
  CHECK(moving.after(0));
  CHECK(moving.after(200));  // past the fade of 150, still shaking
  settled(h, moving);
}

void test_the_picker_comes_and_goes_and_lets_go() {
  UiHarness h;
  room_from_the_pack(h, false);
  Moving moving(h);
  settled(h, moving);
  EmojiPicker& picker = h.app.emoji_picker();
  picker.show("1f600", nullptr);
  CHECK(moving.after(0));
  CHECK(h.drawn(picker, EmojiPicker::Backdrop));
  CHECK(moving.after(80));
  settled(h, moving);
  picker.close();
  CHECK(moving.after(0));
  CHECK(!h.drawn(picker, EmojiPicker::Backdrop));  // going, it is only a picture
  settled(h, moving);
  CHECK(!h.drawn(picker, EmojiPicker::Backdrop));
}

void test_a_card_and_the_changelog_rise_and_end() {
  UiHarness h;
  room_from_the_pack(h, false);
  Moving moving(h);
  settled(h, moving);
  RoomScreen& room = h.app.room();
  room.profile_card().request(0);
  room.show_profile({0, "Me", "", std::nullopt, std::nullopt, 3900, 12345});
  (void)h.app.tick(0);
  CHECK(moving.after(0));
  CHECK(moving.after(80));
  settled(h, moving);
  room.close_member_popups();
  settled(h, moving);
  ChangelogView& view = h.app.changelog();
  view.open({}, 0);
  CHECK(moving.after(0));
  CHECK(moving.after(80));
  settled(h, moving);
  view.close();
  settled(h, moving);
}

void test_a_path_can_fill_where_its_shapes_overlap() {
  UiHarness h;
  CHECK(h.start(100, 100));
  const auto inside = [&](std::string_view data) {
    const auto shape = h.app.ui().gfx.parse_path(data);
    BOOL contains = FALSE;
    CHECK(shape && SUCCEEDED(shape->FillContainsPoint(D2D1::Point2F(5, 5), D2D1::IdentityMatrix(), &contains)));
    return contains != FALSE;
  };
  CHECK(!inside("M0 0H10V10H0ZM0 0H10V10H0Z"));
  CHECK(inside("F1 M0 0H10V10H0ZM0 0H10V10H0Z"));
}

}  // namespace

int main() {
  test_the_rows_choose_a_skin_a_theme_and_a_dark_mode();
  test_the_layers_stand_where_the_code_puts_them();
  test_the_prompt_stands_where_the_code_puts_it_and_clicks_land();
  test_a_list_stands_where_the_code_puts_it();
  test_a_dialog_rises_and_then_asks_for_no_more_frames();
  test_a_toast_comes_goes_and_is_gone();
  test_a_tip_fades_out_before_it_lets_go_of_its_text();
  test_the_offline_popup_rises_once();
  test_a_room_stands_where_the_code_puts_it();
  test_the_volume_popup_stands_where_the_code_puts_it();
  test_what_opens_from_a_room_stands_where_the_code_puts_it();
  test_a_room_at_rest_asks_for_no_frames();
  test_a_disc_draws_only_itself_and_held_up_it_costs_nothing();
  test_someone_arrives_once_and_a_first_list_does_not();
  test_a_message_rises_and_ends();
  test_quieting_everyone_fades_and_ends();
  test_the_chat_shakes_calms_and_ends();
  test_the_send_button_moves_out_for_a_second_line();
  test_a_refused_switch_shakes_its_tip_longer_than_the_tip_fades();
  test_the_picker_comes_and_goes_and_lets_go();
  test_a_card_and_the_changelog_rise_and_end();
  test_a_path_can_fill_where_its_shapes_overlap();
  test_every_tab_stands_where_the_code_puts_it();
  test_clicks_in_the_tabs_do_what_they_do_on_the_code();
  test_a_scroll_asked_before_the_first_frame_waits_for_the_height();
  test_the_panel_comes_and_goes_and_only_the_droplet_takes_clicks_as_it_goes();
  test_a_change_of_tab_fades_and_ends();
  test_scrolling_settles_and_the_bar_goes_away();
  return crpp_test_result();
}
