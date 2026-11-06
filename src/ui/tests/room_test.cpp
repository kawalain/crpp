// The Room screen through a windowless App.
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

}  // namespace

int main() {
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
