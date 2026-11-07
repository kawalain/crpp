// The Room screen through a windowless App.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "check.hpp"
#include "crpp/ui/controls.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
using test::UiHarness;

namespace {

std::vector<MemberView> people(int n) {
  std::vector<MemberView> members;
  for (int i = 0; i < n; ++i) {
    members.push_back({static_cast<std::uint32_t>(i), "사람" + std::to_string(i), "", false, "c" + std::to_string(i)});
  }
  return members;
}

void enter(UiHarness& h, int members = 3, bool chat = false, std::uint32_t me = 0, std::uint32_t host = 0) {
  h.app.show_session("ABC234");
  h.app.room().set_room(people(members), me, host, false, chat);
  h.render();
}

// Hovers the hit and redraws, so what the hover shows is decided.
void hover(UiHarness& h, const Surface& owner, int id) {
  const Ui::Hit* hit = h.app.ui().find_hit(&owner, id);
  CHECK(hit != nullptr);
  if (!hit) return;
  h.app.mouse_move(hit->rect.x + hit->rect.w / 2, hit->rect.y + hit->rect.h / 2);
  h.render();
}

void test_shadow_layers_follow_css() {
  // CSS at the band's middle, for blur 16: 2/3/... px out, sigma 8. The sum from a band
  // outwards is the shadow's strength there, as a share of its alpha.
  const auto strength = [](std::size_t band) {
    float sum = 0;
    for (std::size_t i = band; i < shadow_steps; ++i) sum += shadow_layers()[i];
    return sum;
  };
  CHECK(std::abs(strength(0) - 0.467f) < 0.002f);   // 0.67 px out
  CHECK(std::abs(strength(3) - 0.280f) < 0.002f);   // 4.67 px out
  CHECK(std::abs(strength(11) - 0.0276f) < 0.002f);  // 15.3 px out
  for (std::size_t i = 0; i < shadow_steps; ++i) CHECK(shadow_layers()[i] > 0);
}

void test_a_speaking_disc_fades_in_holds_and_fades_out() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  Ui& ui = h.app.ui();
  ui.animations = true;
  RoomScreen& room = h.app.room();
  const auto start = clock_ms();
  room.pulse(1);
  CHECK(room.only_moving_area().has_value());  // only the avatar has to be drawn
  const auto at = [&](unsigned long long ms) {
    (void)h.app.tick(start + ms);
    return room.speaking_level(1);
  };
  const float rising = at(50);
  CHECK(rising > 0 && rising < 1);
  CHECK(room.animating());
  // Past the rise it stays up with no frames at all, until the fall.
  CHECK(at(110) > 0.99f);
  CHECK(!room.animating());
  CHECK(room.wake_at() >= start + 160);
  room.pulse(1);  // someone keeps typing: nothing to draw
  CHECK(!room.animating());
  (void)at(110);
  const auto end_of_hold = clock_ms() + 160;
  CHECK(room.wake_at() <= end_of_hold);
  const float falling = room.speaking_level(1);
  CHECK(falling > 0.99f);
  (void)h.app.tick(end_of_hold + 50);
  const float low = room.speaking_level(1);
  CHECK(low > 0 && low < 1);
  CHECK(room.animating());
  (void)h.app.tick(end_of_hold + 120);
  CHECK(room.speaking_level(1) == 0);
  CHECK(!room.animating());
  // The last frame still clears it.
  CHECK(room.only_moving_area().has_value());
  h.render();
  CHECK(!room.only_moving_area().has_value());
}

void test_a_disc_asks_for_a_frame_even_without_motion() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  RoomScreen& room = h.app.room();
  CHECK(!h.app.ui().animations);
  CHECK(!room.take_new_pulse());
  room.pulse(1);
  CHECK(room.take_new_pulse());
  CHECK(!room.take_new_pulse());
  room.pulse(1);  // still up
  CHECK(!room.take_new_pulse());
  CHECK(!room.animating());
  CHECK(room.only_moving_area().has_value());
}

void test_nothing_is_drawn_for_someone_muted() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  RoomScreen& room = h.app.room();
  h.app.ui().prefs->set(pref::mute_others, true);
  room.pulse(1);
  CHECK(!room.animating());
  CHECK(room.wake_at() == 0);
  h.app.ui().prefs->set(pref::mute_others, false);
  room.pulse(1);
  CHECK(room.wake_at() != 0);
}

void test_another_surface_changing_asks_for_the_whole_window() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  h.app.ui().animations = true;
  h.app.room().pulse(1);
  CHECK(h.app.only_moving_area().has_value());
  h.app.toasts().toast("안녕");
  CHECK(!h.app.only_moving_area().has_value());
}

void test_button_tips_follow_the_hover() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  RoomScreen& room = h.app.room();
  hover(h, room, RoomScreen::Leave);
  CHECK(room.hover_tip().visible());
  CHECK(room.hover_tip().text() == "구름에서 떠나기");
  hover(h, room, RoomScreen::More);
  CHECK(room.hover_tip().text() == "더보기");
  hover(h, room, RoomScreen::Mute);
  CHECK(room.hover_tip().text() == "음소거");
  h.app.mouse_leave();
  h.render();
  CHECK(!room.hover_tip().visible());
}

void test_open_popup_hides_its_button_tip() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  RoomScreen& room = h.app.room();
  hover(h, room, RoomScreen::More);
  CHECK(h.click(room, RoomScreen::More));
  CHECK(!room.hover_tip().visible());
}

void test_host_and_offline_members_are_labelled() {
  UiHarness h;
  CHECK(h.start());
  auto members = people(3);
  members[1].offline = true;
  h.app.show_session("ABC234");
  h.app.room().set_room(members, 2, 0, false, false);
  h.render();
  RoomScreen& room = h.app.room();
  hover(h, room, RoomScreen::MemberBase + 0);
  CHECK(room.hover_tip().text() == "이 구름의 호스트에요.");
  hover(h, room, RoomScreen::MemberBase + 1);
  CHECK(room.hover_tip().text() == "오프라인");
  hover(h, room, RoomScreen::MemberBase + 2);
  CHECK(!room.hover_tip().visible());
}

void test_copying_shows_the_tip_and_mutes_the_hint() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  RoomScreen& room = h.app.room();
  std::string copied;
  room.on_copy = [&](std::string text) {
    copied = std::move(text);
    return true;
  };
  CHECK(h.click(room, RoomScreen::More));
  hover(h, room, RoomScreen::OptCode);
  CHECK(room.hover_tip().text() == "클릭해 복사");
  CHECK(h.click(room, RoomScreen::OptCode));
  CHECK(copied == "ABC234");
  CHECK(room.copy_tip().visible());
  CHECK(room.copy_tip().text() == "복사 완료 !");
  CHECK(!room.hover_tip().visible());
  // It waits on a single wake-up, not on frames.
  const auto wake = h.app.wake_at();
  CHECK(wake != 0);
  CHECK(h.app.tick(wake));
  h.render();
  CHECK(!room.copy_tip().visible());
}

void test_copy_failure_says_so() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  RoomScreen& room = h.app.room();
  room.on_copy = [](std::string) { return false; };
  CHECK(h.click(room, RoomScreen::More));
  CHECK(h.click(room, RoomScreen::OptCode));
  CHECK(room.copy_tip().text() == "복사 실패");
}

void test_member_list_scrolls_only_when_it_overflows() {
  UiHarness h;
  CHECK(h.start());
  enter(h, 4, true);
  RoomScreen& room = h.app.room();
  const Ui::Hit* hit = h.app.ui().find_hit(&room, RoomScreen::MemberBase + 0);
  CHECK(hit != nullptr);
  const float x = hit->rect.x + 20, y = hit->rect.y + 20;
  h.app.wheel(x, y, -1);
  h.render();
  CHECK(room.members_offset() == 0);

  room.set_room(people(16), 0, 0, false, true);
  h.render();
  // Sixteen rows do not fit; the last is not reachable by the mouse until scrolled to.
  CHECK(!h.drawn(room, RoomScreen::MemberBase + 15));
  for (int i = 0; i < 20; ++i) h.app.wheel(x, y, -1);
  h.render();
  CHECK(room.members_offset() > 0);
  CHECK(h.drawn(room, RoomScreen::MemberBase + 15));
  CHECK(!h.drawn(room, RoomScreen::MemberBase + 0));
}

void test_leave_asks_first() {
  UiHarness h;
  CHECK(h.start());
  enter(h, 1);
  int left = 0;
  h.app.room().on_leave = [&] { ++left; };
  CHECK(h.click(h.app.room(), RoomScreen::Leave));
  CHECK(left == 0);
  CHECK(h.app.dialogs().is_open());
  CHECK(h.app.dialogs().text() == "이 구름에서 떠날까요?");
  h.key(VK_ESCAPE);
  CHECK(!h.app.dialogs().is_open());
  CHECK(left == 0);

  CHECK(h.click(h.app.room(), RoomScreen::Leave));
  CHECK(h.click(h.app.dialogs(), Dialogs::Confirm));
  CHECK(left == 1);
}

void test_leaving_host_is_told_about_delegation() {
  UiHarness h;
  CHECK(h.start());
  enter(h, 2, false, 0, 0);
  CHECK(h.click(h.app.room(), RoomScreen::Leave));
  CHECK(h.app.dialogs().text() == "이 구름에서 떠날까요?\n호스트를 무작위로 다른 사람에게 위임합니다.");
  h.key(VK_ESCAPE);
  h.app.room().set_room(people(2), 1, 0, false, false);  // someone else is Host
  h.render();
  CHECK(h.click(h.app.room(), RoomScreen::Leave));
  CHECK(h.app.dialogs().text() == "이 구름에서 떠날까요?");
}

void test_chat_notice_shows_once_then_the_pin_tip_once() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  RoomScreen& room = h.app.room();
  std::vector<bool> sent;
  room.on_set_chat = [&](bool on) { sent.push_back(on); };
  CHECK(h.click(room, RoomScreen::More));
  CHECK(h.click(room, RoomScreen::OptChat));
  CHECK(h.app.dialogs().text().find("5분이 지나면") != std::string_view::npos);
  CHECK(sent.empty());
  CHECK(h.click(h.app.dialogs(), Dialogs::Confirm));
  CHECK(sent.size() == 1 && sent[0]);
  CHECK(h.drawn(room, RoomScreen::OptChat));  // the switches stay in view behind it
  // The first answer brings the tip, which has no cancel and survives Escape.
  CHECK(h.app.dialogs().text().find("기본값으로 고정") != std::string_view::npos);
  h.key(VK_ESCAPE);
  CHECK(h.app.dialogs().is_open());
  CHECK(h.click(h.app.dialogs(), Dialogs::Confirm));
  CHECK(!h.app.dialogs().is_open());

  // Seen: straight through, and turning it on again asks nothing.
  CHECK(h.click(room, RoomScreen::OptChat));
  CHECK(sent.size() == 2 && sent[1]);
  CHECK(!h.app.dialogs().is_open());
}

void test_public_notice_cancel_changes_nothing_but_counts_as_seen() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  RoomScreen& room = h.app.room();
  int calls = 0;
  room.on_set_public = [&](bool) { ++calls; };
  CHECK(h.click(room, RoomScreen::More));
  CHECK(h.click(room, RoomScreen::OptPublic));
  CHECK(h.app.dialogs().text().find("공개 구름으로 전환") != std::string_view::npos);
  h.key(VK_ESCAPE);
  CHECK(calls == 0);
  CHECK(h.app.ui().prefs->get(pref::public_notice_seen));
  CHECK(h.app.dialogs().text().find("기본값으로 고정") != std::string_view::npos);
  CHECK(h.click(h.app.dialogs(), Dialogs::Confirm));
  // The pin tip is gone for good once either notice brought it.
  CHECK(h.click(room, RoomScreen::OptChat));
  CHECK(h.click(h.app.dialogs(), Dialogs::Cancel));
  CHECK(!h.app.dialogs().is_open());
}

void test_only_the_host_moves_the_switches() {
  UiHarness h;
  CHECK(h.start());
  enter(h, 3, false, 1, 0);
  RoomScreen& room = h.app.room();
  int calls = 0;
  room.on_set_public = [&](bool) { ++calls; };
  room.on_set_chat = [&](bool) { ++calls; };
  CHECK(h.click(room, RoomScreen::More));
  CHECK(h.click(room, RoomScreen::OptPublic));
  CHECK(h.click(room, RoomScreen::OptChat));
  CHECK(calls == 0);
  CHECK(!h.app.dialogs().is_open());
  hover(h, room, RoomScreen::OptChat);
  CHECK(room.hover_tip().text() == "호스트만 조작할 수 있습니다.");
}

bool right_click(UiHarness& h, const Surface& owner, int id) {
  const Ui::Hit* hit = h.app.ui().find_hit(&owner, id);
  if (!hit) return false;
  const bool used = h.app.right_click(hit->rect.x + hit->rect.w / 2, hit->rect.y + hit->rect.h / 2);
  h.render();
  return used;
}

void test_right_click_pins_an_on_switch_and_a_pin_blocks_turning_off() {
  UiHarness h;
  CHECK(h.start());
  Prefs& prefs = *h.app.ui().prefs;
  prefs.set(pref::public_notice_seen, true);
  enter(h);
  RoomScreen& room = h.app.room();
  int calls = 0;
  room.on_set_public = [&](bool) { ++calls; };
  CHECK(h.click(room, RoomScreen::More));
  CHECK(right_click(h, room, RoomScreen::OptPublic));
  CHECK(!prefs.get(pref::pin_public));  // off cannot be pinned

  room.set_room(people(3), 0, 0, true, false);
  h.render();
  hover(h, room, RoomScreen::OptPublic);
  CHECK(room.hover_tip().text() == "우클릭해 활성화 상태를\n기본값으로 고정합니다.");
  CHECK(right_click(h, room, RoomScreen::OptPublic));
  CHECK(prefs.get(pref::pin_public));
  CHECK(room.hover_tip().text() == "우클릭해 기본값 고정을 해제합니다.");

  CHECK(h.click(room, RoomScreen::OptPublic));
  CHECK(calls == 0);
  CHECK(room.hover_tip().text() == "우클릭하여 기본값 고정 해제 후\n조작할 수 있습니다.");
  h.app.mouse_leave();
  h.render();
  hover(h, room, RoomScreen::OptPublic);
  CHECK(room.hover_tip().text() == "우클릭해 기본값 고정을 해제합니다.");

  CHECK(right_click(h, room, RoomScreen::OptPublic));
  CHECK(!prefs.get(pref::pin_public));
  CHECK(h.click(room, RoomScreen::OptPublic));
  CHECK(calls == 1);
}

void test_chat_cannot_be_turned_off_after_the_lock() {
  UiHarness h;
  CHECK(h.start());
  h.app.ui().prefs->set(pref::chat_notice_seen, true);
  h.app.show_session("ABC234");
  RoomScreen& room = h.app.room();
  int calls = 0;
  room.on_set_chat = [&](bool) { ++calls; };
  room.set_room(people(3), 0, 0, false, true, 300000);
  h.render();
  CHECK(h.app.wake_at() != 0);  // it wakes once to show the lock
  CHECK(h.click(room, RoomScreen::More));
  CHECK(h.click(room, RoomScreen::OptChat));
  CHECK(calls == 1);

  room.set_room(people(3), 0, 0, false, true, 0);  // the server says no time is left
  h.render();
  CHECK(h.click(room, RoomScreen::OptChat));
  CHECK(calls == 1);
  CHECK(room.hover_tip().text() == "이 구름에서는 이제 채팅을\n비활성화 할 수 없습니다.");
}

void test_losing_the_room_closes_its_question() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  int left = 0;
  h.app.room().on_leave = [&] { ++left; };
  CHECK(h.click(h.app.room(), RoomScreen::Leave));
  h.app.show_home();
  CHECK(!h.app.dialogs().is_open());
  CHECK(left == 0);
}

void test_other_questions_survive_leaving_the_room() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  h.app.dialogs().show({"다른 질문", "네", "", false, nullptr, nullptr});
  h.app.show_home();
  CHECK(h.app.dialogs().is_open());
}

// An emoji folder with as many as the real list, none of them relying on the system's font.
void with_assets(UiHarness& h) {
  namespace fs = std::filesystem;
  static const fs::path dir = [] {
    fs::path folder = fs::temp_directory_path() / "crpp_room_test_assets";
    fs::create_directories(folder / "emoji");
    const char* drawing = R"(<svg viewBox="0 0 36 36"><circle cx="18" cy="18" r="18" fill="#fc0"/></svg>)";
    for (int i = 0; i < 172; ++i) {
      const std::string id = i < 165 ? "1f" + std::to_string(100 + i) : "0" + std::to_string(i - 164);
      std::ofstream(folder / "emoji" / (id + ".svg")) << drawing;
    }
    return folder;
  }();
  std::vector<std::string> ids;
  for (int i = 0; i < 172; ++i) ids.push_back(i < 165 ? "1f" + std::to_string(100 + i) : "0" + std::to_string(i - 164));
  h.app.ui().emoji_ids = std::move(ids);
  h.app.reload_assets(dir);
}

void test_a_new_message_rises_but_a_history_does_not() {
  UiHarness h;
  CHECK(h.start());
  h.app.show_session("ABC234");
  h.app.room().set_room({{0, "a", "", false}}, 0, 0, false, true);
  ChatPanel& chat = h.app.room().chat();
  chat.add({0, "c0", "a", "", "before", 1700000000000});
  hold_clock_at(1000000);
  h.app.ui().animations = true;
  h.render();
  hold_clock_at(1100000);
  h.render();  // the view's own transition is long over
  CHECK(!h.app.ui().more_frames);

  chat.add({0, "c0", "a", "", "new", 1700000060000});
  h.render();
  CHECK(h.app.ui().more_frames);
  hold_clock_at(1100000 + 400);
  h.render();
  hold_clock_at(1100000 + 800);
  h.render();
  CHECK(!h.app.ui().more_frames);  // settled, it asks for nothing
  CHECK(chat.shown() == 2);

  // Reading older messages: what arrives does not move the list.
  for (int i = 0; i < 60; ++i) chat.add({0, "c0", "a", "", "line", 1700000120000});
  hold_clock_at(1200000);
  h.render();
  hold_clock_at(1300000);
  h.render();
  h.app.wheel(800, 200, 1);
  hold_clock_at(1400000);
  h.render();
  hold_clock_at(1500000);
  h.render();
  const float reading = chat.scroll_offset();
  chat.add({0, "c0", "a", "", "while reading", 1700000180000});
  h.render();
  CHECK(chat.scroll_offset() == reading);
  hold_clock_at(0);
}

void test_a_room_stays_as_it_was_while_it_fades_out() {
  UiHarness h;
  CHECK(h.start());
  h.app.show_session("ABC234");
  h.app.room().set_room({{0, "a", "", false}}, 0, 0, false, true);
  h.app.ui().animations = true;
  hold_clock_at(1000000);
  h.render();
  hold_clock_at(1100000);
  h.render();
  h.app.show_home();
  CHECK(h.app.view() == View::Home);  // input goes to the new screen at once
  CHECK(h.app.room().member_count() == 1);
  h.render();
  CHECK(h.app.ui().more_frames);
  CHECK(h.app.ui().find_hit(&h.app.room(), RoomScreen::MemberBase) == nullptr);  // it takes no clicks
  hold_clock_at(1100000 + 200);
  h.render();
  CHECK(h.app.room().member_count() == 0);
  CHECK(!h.app.ui().more_frames);
  hold_clock_at(0);
}

void test_a_member_arrives_and_a_settled_room_asks_for_no_frames() {
  UiHarness h;
  CHECK(h.start());
  h.app.show_session("ABC234");
  h.app.room().set_room({{0, "a", "", false}}, 0, 0, false, true);
  h.app.ui().animations = true;
  hold_clock_at(1000000);
  h.render();
  hold_clock_at(1100000);
  h.render();
  CHECK(!h.app.ui().more_frames);

  h.app.room().set_room({{0, "a", "", false}, {1, "b", "", false}}, 0, 0, false, true);
  h.render();
  CHECK(h.app.ui().more_frames);
  hold_clock_at(1100000 + 400);
  h.render();
  CHECK(!h.app.ui().more_frames);
  hold_clock_at(0);
}

void test_the_settings_panel_fades_out_after_it_closed() {
  UiHarness h;
  CHECK(h.start());
  h.app.ui().animations = true;
  hold_clock_at(1000000);
  h.render();
  h.app.settings().open(SettingsPage::Sound);
  h.render();
  hold_clock_at(1100000);
  h.render();
  CHECK(!h.app.animating());

  h.app.settings().close();
  CHECK(!h.app.settings().is_open());  // it takes no more input
  h.render();
  CHECK(h.app.animating());
  hold_clock_at(1100000 + 400);
  h.render();
  CHECK(!h.app.animating());
  hold_clock_at(0);
}

void show_everything_that_keeps_graphics(UiHarness& h) {
  h.app.show_prompt({.heading = "h", .password = true});
  CHECK(h.render());
  h.app.show_session("ABC234");
  h.app.room().set_room({{0, "a", "", false}}, 0, 0, false, true);
  h.app.room().chat().add({0, "c0", "a", "", "hello", 1700000000000});
  CHECK(h.render());
  h.app.show_home();
  CHECK(h.render());
  for (const SettingsPage page : {SettingsPage::Sound, SettingsPage::Display, SettingsPage::Notify, SettingsPage::System, SettingsPage::Account}) {
    h.app.settings().open(page);
    CHECK(h.render());
  }
  h.app.settings().close();
  h.app.changelog().open({}, 0);
  CHECK(h.render());
  h.app.emoji_picker().show("", [](std::string) {});
  CHECK(h.render());
}

void test_releasing_graphics_lets_go_of_the_factory() {
  UiHarness h;
  CHECK(h.start());
  with_assets(h);
  show_everything_that_keeps_graphics(h);
  CHECK(h.app.emoji_picker().is_open());

  // Anything made from the factory counts on it, so a count of zero at the end means
  // no holder is left. `held` shows that the count would notice one.
  h.drop_target();
  ID2D1Factory* const factory = h.app.factory();
  factory->AddRef();
  auto held = h.app.ui().gfx.parse_path("M0 0L1 1");
  h.app.release_graphics();
  factory->AddRef();
  CHECK(factory->Release() >= 2);
  held = {};
  CHECK(factory->Release() == 0);

  // All of it comes back, from the new factory, or Direct2D refuses the frame.
  CHECK(h.make_target());
  CHECK(h.render());
  h.app.emoji_picker().close();
  show_everything_that_keeps_graphics(h);
  h.app.emoji_picker().close();
  CHECK(h.render());
}

void test_only_plain_ids_name_an_emoji_file() {
  CHECK(valid_emoji_id("1f4a7"));
  CHECK(valid_emoji_id("1f408-200d-2b1b"));
  CHECK(valid_emoji_id("01"));
  CHECK(!valid_emoji_id(""));
  CHECK(!valid_emoji_id("../../secret"));
  CHECK(!valid_emoji_id("C:\\secret"));
  CHECK(!valid_emoji_id("a/b"));
  CHECK(!valid_emoji_id(std::string(65, 'a')));
}

void test_picker_hides_what_it_cannot_show() {
  UiHarness h;
  CHECK(h.start());
  with_assets(h);
  h.app.ui().emoji_ids = {"1f100", "99", "1f999999"};
  EmojiPicker& picker = h.app.emoji_picker();
  picker.show("", nullptr);
  // A hand-made id without its file has no glyph anywhere, and no code point is that big.
  CHECK((picker.ids() == std::vector<std::string>{"1f100"}));
  picker.close();
}

void test_picker_lists_every_emoji_and_parses_only_what_shows() {
  UiHarness h;
  CHECK(h.start());
  with_assets(h);
  EmojiPicker& picker = h.app.emoji_picker();
  picker.show("1f600", nullptr);
  h.render();
  CHECK(picker.ids().size() == 172);
  CHECK(picker.ids().front() < picker.ids()[1]);
  CHECK(picker.ids().back() == "07");  // the hand-made ones go last
  CHECK(picker.parsed() > 0);
  CHECK(picker.parsed() <= 56);
  CHECK(h.drawn(picker, EmojiPicker::CellBase));
  CHECK(!h.drawn(picker, EmojiPicker::CellBase + 171));

  // All the way down and back: never more than the cache holds.
  for (int i = 0; i < 40; ++i) {
    h.app.wheel(475, 346, -0.5f);
    h.render();
    CHECK(picker.parsed() <= EmojiPicker::cache_capacity);
  }
  CHECK(h.drawn(picker, EmojiPicker::CellBase + 171));
  CHECK(!h.drawn(picker, EmojiPicker::CellBase));

  picker.close();
  CHECK(picker.parsed() == 0);
  CHECK(picker.ids().empty());
}

void test_picker_picks_once_and_closes() {
  UiHarness h;
  CHECK(h.start());
  with_assets(h);
  EmojiPicker& picker = h.app.emoji_picker();
  std::vector<std::string> picked;
  picker.show("", [&](std::string id) { picked.push_back(std::move(id)); });
  h.render();
  const std::string second = picker.ids()[1];
  CHECK(h.click(picker, EmojiPicker::CellBase + 1));
  CHECK(picked.size() == 1 && picked[0] == second);
  CHECK(!picker.is_open());
}

void test_picker_closes_without_picking() {
  UiHarness h;
  CHECK(h.start());
  with_assets(h);
  EmojiPicker& picker = h.app.emoji_picker();
  int picks = 0;
  picker.show("", [&](std::string) { ++picks; });
  h.render();
  h.key(VK_ESCAPE);
  CHECK(!picker.is_open());
  picker.show("", [&](std::string) { ++picks; });
  h.render();
  CHECK(h.click(picker, EmojiPicker::Card));  // inside, between the emoji: stays
  CHECK(picker.is_open());
  h.app.mouse_down(5, 5, false);  // the backdrop
  CHECK(!picker.is_open());
  CHECK(picks == 0);
}

void test_picker_thumb_scrolls_and_fades_on_a_wake_up() {
  UiHarness h;
  CHECK(h.start());
  with_assets(h);
  EmojiPicker& picker = h.app.emoji_picker();
  picker.show("", nullptr);
  h.render();
  CHECK(h.app.wake_at() == 0);  // nothing waits until it is touched
  CHECK(h.drawn(picker, EmojiPicker::Thumb));
  h.app.wheel(475, 346, -1);
  const auto wake = h.app.wake_at();
  CHECK(wake != 0);
  CHECK(h.app.tick(wake));
  CHECK(h.app.wake_at() == 0);
}

ProfileView profile_of(std::uint32_t slot) { return {slot, "사람", "", std::nullopt, std::nullopt, 3900, 12345}; }

void test_stay_and_count_read_like_upstream() {
  CHECK(format_stay(0) == "1분 미만");
  CHECK(format_stay(59) == "1분 미만");
  CHECK(format_stay(60) == "1분");
  CHECK(format_stay(3900) == "1시간 5분");
  CHECK(format_stay(3600) == "1시간");
  CHECK(format_stay((525600 + 43200 + 60) * 60LL) == "1년 1개월 1시간");
  CHECK(format_count(0) == "0회");
  CHECK(format_count(999) == "999회");
  CHECK(format_count(1000) == "1,000회");
  CHECK(format_count(1234567) == "1,234,567회");
}

void test_clicking_a_member_asks_and_the_answer_opens_the_card() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  RoomScreen& room = h.app.room();
  std::vector<std::uint32_t> asked;
  room.on_request_profile = [&](std::uint32_t slot) { asked.push_back(slot); };
  CHECK(h.click(room, RoomScreen::MemberBase + 1));
  CHECK(asked.size() == 1 && asked[0] == 1);
  CHECK(!room.profile_card().is_open());
  room.show_profile(profile_of(1));
  h.render();
  CHECK(room.profile_card().is_open());
  CHECK(!h.drawn(room.profile_card(), ProfileCard::Icon));  // not mine
  h.key(VK_ESCAPE);
  CHECK(!room.profile_card().is_open());
}

void test_a_late_refresh_does_not_reopen_the_card() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  RoomScreen& room = h.app.room();
  room.show_profile(profile_of(1));  // nobody asked
  CHECK(!room.profile_card().is_open());
  CHECK(h.click(room, RoomScreen::MemberBase + 1));
  room.show_profile(profile_of(1));
  h.render();
  CHECK(room.profile_card().is_open());
  h.app.mouse_down(3, 3, false);  // the backdrop closes it
  CHECK(!room.profile_card().is_open());
  room.show_profile(profile_of(1));
  CHECK(!room.profile_card().is_open());
}

void test_the_open_card_refreshes_and_an_unanswered_one_gives_up() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  RoomScreen& room = h.app.room();
  int asked = 0;
  room.on_request_profile = [&](std::uint32_t) { ++asked; };
  CHECK(h.click(room, RoomScreen::MemberBase + 1));
  const auto give_up = h.app.wake_at();
  CHECK(give_up != 0);
  room.show_profile(profile_of(1));
  h.render();
  CHECK(asked == 1);
  h.app.tick(h.app.wake_at());
  CHECK(asked == 2);
  auto updated = profile_of(1);
  updated.events = 7;
  room.show_profile(updated);
  CHECK(room.profile_card().profile().events == 7);
  h.key(VK_ESCAPE);
  CHECK(h.app.wake_at() == 0);  // closed: nothing keeps asking

  CHECK(h.click(room, RoomScreen::MemberBase + 2));
  CHECK(h.app.tick(h.app.wake_at()));
  CHECK(h.app.wake_at() != 0);  // the toast's own wake-up
  room.show_profile(profile_of(2));  // too late
  CHECK(!room.profile_card().is_open());
}

void test_own_card_changes_the_icon() {
  UiHarness h;
  CHECK(h.start());
  with_assets(h);
  enter(h);
  RoomScreen& room = h.app.room();
  std::string picked;
  room.on_pick_emoji = [&](const std::string& id) { picked = id; };
  room.profile_card().request(0);
  room.show_profile(profile_of(0));
  h.render();
  CHECK(room.profile_card().mine());
  CHECK(h.click(room.profile_card(), ProfileCard::Icon));
  EmojiPicker& picker = h.app.emoji_picker();
  CHECK(picker.is_open());
  const std::string first = picker.ids()[0];
  CHECK(h.click(picker, EmojiPicker::CellBase));
  CHECK(picked == first);
  CHECK(room.profile_card().profile().emoji == first);
  CHECK(room.profile_card().is_open());

  // Leaving the Room takes an open picker along.
  CHECK(h.click(room.profile_card(), ProfileCard::Icon));
  h.app.show_home();
  CHECK(!picker.is_open());
  CHECK(!room.profile_card().is_open());
}

void test_the_card_covers_the_settings_droplet() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  RoomScreen& room = h.app.room();
  room.profile_card().request(1);
  room.show_profile(profile_of(1));
  h.render();
  const Ui::Hit* droplet = h.app.ui().find_hit(&h.app.settings(), SettingsPanel::Droplet);
  CHECK(droplet != nullptr);
  if (droplet) h.app.mouse_down(droplet->rect.x + 5, droplet->rect.y + 5, false);
  CHECK(!h.app.settings().is_open());
}

void test_two_lines_move_the_send_button_out_of_the_box() {
  UiHarness h;
  CHECK(h.start());
  enter(h, 3, true);
  ChatPanel& chat = h.app.room().chat();
  h.render();
  const Ui::Hit* send = h.app.ui().find_hit(&chat, ChatPanel::Send);
  const Ui::Hit* box = h.app.ui().find_hit(&chat, ChatPanel::Input);
  CHECK(send && box);
  if (!send || !box) return;
  CHECK(send->rect.bottom() <= box->rect.bottom());
  chat.input().set_text(L"첫 줄\n둘째 줄");
  h.render();
  send = h.app.ui().find_hit(&chat, ChatPanel::Send);
  box = h.app.ui().find_hit(&chat, ChatPanel::Input);
  CHECK(send && box);
  if (send && box) CHECK(send->rect.y >= box->rect.bottom());
}

void test_mute_is_a_saved_setting() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  RoomScreen& room = h.app.room();
  std::vector<bool> told;
  room.on_mute = [&](bool muted) { told.push_back(muted); };
  CHECK(h.click(room, RoomScreen::Mute));
  CHECK(h.app.ui().prefs->get(pref::mute_others));
  CHECK(told.size() == 1 && told[0]);
  room.reset();
  CHECK(h.app.ui().prefs->get(pref::mute_others));  // it outlives the Room
  enter(h);
  CHECK(h.click(room, RoomScreen::Mute));
  CHECK(!h.app.ui().prefs->get(pref::mute_others));
  CHECK(told.size() == 2 && !told[1]);
}

void test_member_menu_for_someone_else() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  RoomScreen& room = h.app.room();
  MemberMenu& menu = room.member_menu();
  std::vector<std::pair<std::string, int>> volumes;
  std::vector<std::pair<std::string, bool>> mutes;
  room.on_member_volume = [&](const std::string& cid, int v) { volumes.emplace_back(cid, v); };
  room.on_member_muted = [&](const std::string& cid, bool m) { mutes.emplace_back(cid, m); };
  CHECK(right_click(h, room, RoomScreen::MemberBase + 1));
  CHECK(menu.is_open());
  CHECK(menu.volume() == 100);
  CHECK(h.drawn(menu, MemberMenu::Slider));

  CHECK(h.click(menu, MemberMenu::Mute));
  CHECK(menu.is_open());  // stays for another change
  CHECK(menu.muted());
  CHECK(volumes.size() == 1 && volumes[0].first == "c1" && volumes[0].second == 0);
  CHECK(mutes.size() == 1 && mutes[0].second);

  CHECK(h.click(menu, MemberMenu::Mute));
  CHECK(!menu.muted());
  CHECK(volumes.size() == 2 && volumes[1].second == 100);
  CHECK(mutes.size() == 2 && !mutes[1].second);

  // The slider's middle: not a mute, and remembered for the next opening.
  CHECK(h.click(menu, MemberMenu::Slider));
  CHECK(menu.volume() == 50);
  CHECK(mutes.size() == 2);
  h.app.mouse_down(3, 3, false);
  CHECK(!menu.is_open());
  CHECK(right_click(h, room, RoomScreen::MemberBase + 1));
  CHECK(menu.volume() == 50);
  h.key(VK_ESCAPE);
  CHECK(!menu.is_open());
}

void test_member_menu_for_myself() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  RoomScreen& room = h.app.room();
  MemberMenu& menu = room.member_menu();
  int asked = 0;
  room.on_request_profile = [&](std::uint32_t) { ++asked; };
  CHECK(right_click(h, room, RoomScreen::MemberBase + 0));
  CHECK(!h.drawn(menu, MemberMenu::Slider));
  CHECK(!h.drawn(menu, MemberMenu::Mute));  // shown disabled, takes no click
  CHECK(h.click(menu, MemberMenu::Profile));
  CHECK(!menu.is_open());
  CHECK(asked == 1);
}

void test_right_click_elsewhere_closes_the_menu() {
  UiHarness h;
  CHECK(h.start());
  enter(h);
  RoomScreen& room = h.app.room();
  CHECK(right_click(h, room, RoomScreen::MemberBase + 1));
  h.app.right_click(3, 3);
  CHECK(!room.member_menu().is_open());
}

std::wstring slice(std::wstring_view text, LinkSpan link) { return std::wstring(text.substr(link.start, link.length)); }

void test_links_are_https_with_punctuation_left_out() {
  const auto one = [](std::wstring_view text) {
    const auto links = find_links(text);
    return links.size() == 1 ? slice(text, links[0]) : std::wstring(L"<none>");
  };
  CHECK(one(L"https://a.com") == L"https://a.com");
  CHECK(one(L"봐요 https://a.com/b?c=1#d 여기") == L"https://a.com/b?c=1#d");
  CHECK(one(L"https://a.com.") == L"https://a.com");
  CHECK(one(L"https://a.com/x!?,") == L"https://a.com/x");
  CHECK(one(L"(https://a.com/x)") == L"https://a.com/x");
  CHECK(one(L"https://w.org/w/%EC(%EC)") == L"https://w.org/w/%EC(%EC)");  // its own bracket stays
  CHECK(one(L"https://a.com/x]") == L"https://a.com/x");
  CHECK(find_links(L"http://a.com").empty());
  CHECK(find_links(L"https://").empty());
  CHECK(find_links(L"https://.a").empty());
  CHECK(find_links(L"그냥 글").empty());
  const std::wstring_view two = L"https://a.b, https://c.d";
  const auto links = find_links(two);
  CHECK(links.size() == 2);
  if (links.size() == 2) {
    CHECK(slice(two, links[0]) == L"https://a.b");
    CHECK(slice(two, links[1]) == L"https://c.d");
  }
}

void test_limiter_allows_a_burst_then_cools_down() {
  ChatLimiter l;
  for (int i = 0; i < 10; ++i) CHECK(l.try_send(1000 + static_cast<unsigned>(i)));
  CHECK(!l.calm());
  CHECK(!l.try_send(1100));  // the eleventh
  CHECK(l.calm());
  CHECK(l.cooling());
  CHECK(l.calm_text() == "진정하세요 ! ! !");
  CHECK(!l.try_send(1200));  // one more attempt, one more mark
  CHECK(l.calm_text() == "진정하세요 ! ! ! !");

  CHECK(l.wake_at() == 1200 + 5000 - 150);
  CHECK(l.expire(l.wake_at()));
  CHECK(!l.cooling());
  CHECK(l.calm());  // the bubble lingers
  CHECK(l.try_send(1200 + 5000 - 150));  // and the count started over
  CHECK(l.wake_at() == 1200 + 5000 + 1000);
  CHECK(l.expire(l.wake_at()));
  CHECK(!l.calm());
  CHECK(l.wake_at() == 0);
}

void test_limiter_forgets_old_messages() {
  ChatLimiter l;
  for (int i = 0; i < 10; ++i) CHECK(l.try_send(1000));
  CHECK(l.try_send(11001));  // the window has passed
}

void test_limiter_marks_stop_at_twenty() {
  ChatLimiter l;
  for (int i = 0; i < 10; ++i) CHECK(l.try_send(1000));
  for (unsigned i = 0; i < 40; ++i) CHECK(!l.try_send(1100 + i));
  CHECK(l.calm_text() == "진정하세요 ! ! ! ! ! ! ! ! ! ! ! ! ! ! ! ! ! ! ! !");
}

void test_limiter_tells_foreseen_from_unforeseen_blocks() {
  ChatLimiter l;
  CHECK(l.server_blocked(1000, 5000));  // nothing here saw it coming
  CHECK(l.calm());
  CHECK(!l.try_send(1100));
  CHECK(!l.server_blocked(1101, 5000));  // the answer to that attempt
  CHECK(l.calm_text() == "진정하세요 ! ! ! !");  // counted once, not twice
}

void send_text(UiHarness& h, ChatPanel& chat, const wchar_t* text) {
  chat.input().set_text(text);
  CHECK(h.click(chat, ChatPanel::Send));
}

void test_chat_blocks_the_eleventh_and_keeps_its_text() {
  UiHarness h;
  CHECK(h.start());
  enter(h, 3, true);
  ChatPanel& chat = h.app.room().chat();
  int sent = 0;
  chat.on_send = [&](std::string) {
    ++sent;
    return true;
  };
  for (int i = 0; i < 10; ++i) send_text(h, chat, L"안녕");
  CHECK(sent == 10);
  CHECK(chat.input().text().empty());
  CHECK(!chat.limiter().calm());

  send_text(h, chat, L"또");
  CHECK(sent == 11);  // the server hears it too, and extends its own block
  CHECK(chat.input().text() == L"또");
  CHECK(chat.limiter().calm());
  CHECK(chat.limiter().cooling());
  CHECK(h.app.wake_at() != 0);

  // Its answer changes nothing more; the block stays for the next one.
  chat.input().clear();
  chat.blocked(5000);
  CHECK(chat.input().text().empty());
}

void test_an_unforeseen_block_brings_the_message_back() {
  UiHarness h;
  CHECK(h.start());
  enter(h, 3, true);
  ChatPanel& chat = h.app.room().chat();
  chat.on_send = [](std::string) { return true; };
  send_text(h, chat, L"안녕");
  CHECK(chat.input().text().empty());
  chat.blocked(5000);
  h.render();
  CHECK(chat.input().text() == L"안녕");
  CHECK(chat.limiter().calm());
  // And it does not overwrite what is being typed.
  chat.input().set_text(L"다른 글");
  chat.blocked(5000);
  CHECK(chat.input().text() == L"다른 글");
}

void test_send_failure_keeps_the_text_and_says_so() {
  UiHarness h;
  CHECK(h.start());
  enter(h, 3, true);
  ChatPanel& chat = h.app.room().chat();
  chat.on_send = [](std::string) { return false; };
  send_text(h, chat, L"안녕");
  CHECK(chat.input().text() == L"안녕");
  CHECK(h.app.wake_at() != 0);  // the toast
}

ChatEntry entry_with(std::string text) { return {1, "c1", "하늘", "", std::move(text), 1700000000000}; }

void test_link_opens_through_the_seam_and_shows_its_tip() {
  UiHarness h;
  CHECK(h.start());
  enter(h, 3, true);
  ChatPanel& chat = h.app.room().chat();
  std::vector<std::string> opened;
  bool works = true;
  chat.on_open_link = [&](const std::string& url) {
    opened.push_back(url);
    return works;
  };
  chat.add(entry_with("자료는 https://example.com/a. 입니다"));
  h.render();
  const int link = ChatPanel::LinkBase;
  CHECK(h.drawn(chat, link));
  CHECK(h.drawn(chat, ChatPanel::Area));
  hover(h, chat, link);
  CHECK(chat.link_tip().visible());
  CHECK(chat.link_tip().text() == "링크 연결");
  CHECK(h.app.cursor() == Cursor::Hand);
  CHECK(h.click(chat, link));
  CHECK(opened.size() == 1 && opened[0] == "https://example.com/a");
  CHECK(h.app.wake_at() == 0);
  works = false;
  CHECK(h.click(chat, link));
  CHECK(h.app.wake_at() != 0);  // "링크를 열지 못했어요."
  h.app.mouse_leave();
  h.render();
  CHECK(!chat.link_tip().visible());
}

void test_a_drawn_link_opens_its_own_message_when_the_log_dropped_one() {
  UiHarness h;
  CHECK(h.start());
  enter(h, 3, true);
  ChatPanel& chat = h.app.room().chat();
  std::vector<std::string> opened;
  chat.on_open_link = [&](const std::string& url) {
    opened.push_back(url);
    return true;
  };
  for (int i = 0; i < 1000; ++i) chat.add(entry_with("https://example.com/" + std::to_string(i)));
  h.render();
  const int link = ChatPanel::LinkBase + 998 * ChatPanel::links_per_entry;
  CHECK(h.drawn(chat, link));
  // The oldest goes before the next paint; what is on screen is still the old list.
  chat.add(entry_with("https://example.com/new"));
  CHECK(h.click(chat, link));
  CHECK(opened.size() == 1 && opened[0] == "https://example.com/998");
  // Drawn again, the same message keeps its id.
  CHECK(h.drawn(chat, link));
  CHECK(h.click(chat, link));
  CHECK(opened.size() == 2 && opened[1] == "https://example.com/998");
}

void test_a_drawn_link_opens_its_own_message_when_a_page_was_added_above() {
  UiHarness h;
  CHECK(h.start());
  enter(h, 3, true);
  ChatPanel& chat = h.app.room().chat();
  std::vector<std::string> opened;
  chat.on_open_link = [&](const std::string& url) {
    opened.push_back(url);
    return true;
  };
  // Every other link starts further in, so a span of the wrong row cuts it wrongly.
  for (int i = 0; i < 131; ++i) chat.add(entry_with((i % 2 ? "말 " : "") + ("https://example.com/" + std::to_string(i))));
  h.render();
  CHECK(chat.shown() == 100);
  const Ui::Hit* area = h.app.ui().find_hit(&chat, ChatPanel::Area);
  CHECK(area != nullptr);
  const float x = area->rect.x + 30, y = area->rect.y + 30;
  while (chat.scroll_offset() >= 140) {  // a notch is 80, and under 60 the page above comes
    h.app.wheel(x, y, 1);
    h.render();
  }
  // This wheel lays out the page above at the next paint, which has not come.
  h.app.wheel(x, y, 1);
  const int link = ChatPanel::LinkBase + 36 * ChatPanel::links_per_entry;
  CHECK(h.click(chat, link));
  CHECK(opened.size() == 1 && opened[0] == "https://example.com/36");
  CHECK(chat.shown() == 131);
}

void test_cursors_of_the_chat() {
  UiHarness h;
  CHECK(h.start());
  enter(h, 3, true);
  ChatPanel& chat = h.app.room().chat();
  h.render();
  hover(h, chat, ChatPanel::Input);
  CHECK(h.app.cursor() == Cursor::IBeam);
  hover(h, chat, ChatPanel::Send);
  CHECK(h.app.cursor() == Cursor::Hand);
}

void test_older_messages_come_by_the_page_and_nothing_jumps() {
  UiHarness h;
  CHECK(h.start());
  enter(h, 3, true);
  ChatPanel& chat = h.app.room().chat();
  for (int i = 0; i < 250; ++i) chat.add(entry_with("메시지 " + std::to_string(i)));
  h.render();
  CHECK(chat.log_size() == 250);
  CHECK(chat.shown() == 100);

  // Up to the top of the first page.
  const Ui::Hit* area = h.app.ui().find_hit(&chat, ChatPanel::Area);
  CHECK(area != nullptr);
  const float x = area->rect.x + 30, y = area->rect.y + 30;
  while (chat.shown() == 100) {
    h.app.wheel(x, y, 50);
    h.render();
  }
  CHECK(chat.shown() == 200);
  CHECK(chat.scroll_offset() > 60);  // stayed on the same messages

  while (chat.shown() == 200) {
    h.app.wheel(x, y, 50);
    h.render();
  }
  CHECK(chat.shown() == 250);

  // New ones still arrive at the end, one measured at a time.
  chat.add(entry_with("새 메시지"));
  h.render();
  CHECK(chat.shown() == 251);
}

void test_clearing_the_chat_starts_over() {
  UiHarness h;
  CHECK(h.start());
  enter(h, 3, true);
  ChatPanel& chat = h.app.room().chat();
  for (int i = 0; i < 150; ++i) chat.add(entry_with("메시지"));
  h.render();
  chat.blocked(5000);
  chat.clear();
  CHECK(!chat.limiter().calm());
  for (int i = 0; i < 120; ++i) chat.add(entry_with("다른 구름"));
  h.render();
  CHECK(chat.shown() == 100);
  CHECK(h.drawn(chat, ChatPanel::Area));
}

}  // namespace

int main() {
  test_shadow_layers_follow_css();
  test_a_speaking_disc_fades_in_holds_and_fades_out();
  test_a_disc_asks_for_a_frame_even_without_motion();
  test_nothing_is_drawn_for_someone_muted();
  test_another_surface_changing_asks_for_the_whole_window();
  test_two_lines_move_the_send_button_out_of_the_box();
  test_mute_is_a_saved_setting();
  test_links_are_https_with_punctuation_left_out();
  test_limiter_allows_a_burst_then_cools_down();
  test_limiter_forgets_old_messages();
  test_limiter_marks_stop_at_twenty();
  test_limiter_tells_foreseen_from_unforeseen_blocks();
  test_chat_blocks_the_eleventh_and_keeps_its_text();
  test_an_unforeseen_block_brings_the_message_back();
  test_send_failure_keeps_the_text_and_says_so();
  test_link_opens_through_the_seam_and_shows_its_tip();
  test_a_drawn_link_opens_its_own_message_when_the_log_dropped_one();
  test_a_drawn_link_opens_its_own_message_when_a_page_was_added_above();
  test_cursors_of_the_chat();
  test_older_messages_come_by_the_page_and_nothing_jumps();
  test_clearing_the_chat_starts_over();
  test_stay_and_count_read_like_upstream();
  test_clicking_a_member_asks_and_the_answer_opens_the_card();
  test_a_late_refresh_does_not_reopen_the_card();
  test_the_open_card_refreshes_and_an_unanswered_one_gives_up();
  test_own_card_changes_the_icon();
  test_the_card_covers_the_settings_droplet();
  test_member_menu_for_someone_else();
  test_member_menu_for_myself();
  test_right_click_elsewhere_closes_the_menu();
  test_a_new_message_rises_but_a_history_does_not();
  test_a_room_stays_as_it_was_while_it_fades_out();
  test_a_member_arrives_and_a_settled_room_asks_for_no_frames();
  test_the_settings_panel_fades_out_after_it_closed();
  test_releasing_graphics_lets_go_of_the_factory();
  test_only_plain_ids_name_an_emoji_file();
  test_picker_hides_what_it_cannot_show();
  test_picker_lists_every_emoji_and_parses_only_what_shows();
  test_picker_picks_once_and_closes();
  test_picker_closes_without_picking();
  test_picker_thumb_scrolls_and_fades_on_a_wake_up();
  test_leave_asks_first();
  test_leaving_host_is_told_about_delegation();
  test_chat_notice_shows_once_then_the_pin_tip_once();
  test_public_notice_cancel_changes_nothing_but_counts_as_seen();
  test_only_the_host_moves_the_switches();
  test_right_click_pins_an_on_switch_and_a_pin_blocks_turning_off();
  test_chat_cannot_be_turned_off_after_the_lock();
  test_losing_the_room_closes_its_question();
  test_other_questions_survive_leaving_the_room();
  test_button_tips_follow_the_hover();
  test_open_popup_hides_its_button_tip();
  test_host_and_offline_members_are_labelled();
  test_copying_shows_the_tip_and_mutes_the_hint();
  test_copy_failure_says_so();
  test_member_list_scrolls_only_when_it_overflows();
  return crpp_test_result();
}
