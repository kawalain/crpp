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

}  // namespace

int main() {
  test_button_tips_follow_the_hover();
  test_open_popup_hides_its_button_tip();
  test_host_and_offline_members_are_labelled();
  test_copying_shows_the_tip_and_mutes_the_hint();
  test_copy_failure_says_so();
  test_member_list_scrolls_only_when_it_overflows();
  return crpp_test_result();
}
