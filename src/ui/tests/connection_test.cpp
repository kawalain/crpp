// The offline popup and what failures say, with a fake GET /health.
#include <functional>
#include <optional>

#include "check.hpp"
#include "crpp/ui/connection_feedback.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
namespace session = crpp::session;
using session::Failure;
using session::State;
using crpp::ui::test::UiHarness;

namespace {

class FakeProbe final : public HealthProbe {
 public:
  void start(std::function<void(bool)> done) override {
    ++asked;
    done_ = std::move(done);
  }
  void answer(bool reachable) {
    auto done = std::move(done_);
    done_ = nullptr;
    done(reachable);
  }
  [[nodiscard]] bool waiting() const { return done_ != nullptr; }
  int asked = 0;

 private:
  std::function<void(bool)> done_;
};

struct Rig {
  UiHarness h;
  FakeProbe probe;
  std::optional<ConnectionFeedback> feedback;
  Rig() {
    CHECK(h.start());
    h.app.show_home();
    feedback.emplace(h.app, probe);
    h.render();
  }
  OfflinePopup& popup() { return h.app.offline(); }
  [[nodiscard]] bool popup_drawn() {
    h.render();
    return h.drawn(h.app.offline(), OfflinePopup::Backdrop);
  }
  void go_offline() {
    feedback->check_server(nullptr);
    probe.answer(false);
  }
  // Runs what the window's wake-up timer would.
  void wake() { h.app.tick(h.app.wake_at()); }
};

void failure_wording_is_upstreams() {
  const auto text = [](Failure f, std::string_view d = {}, bool room = false) { return failure_text(f, d, room); };
  CHECK(text(Failure::ServerError, "not_found") == "구름을 찾지 못했습니다.");
  CHECK(text(Failure::ServerError, "room_full") == "구름이 가득 찼습니다.");
  CHECK(text(Failure::ServerError, "already_in_room") == "이미 구름에 참가 중입니다.");
  CHECK(text(Failure::ServerError, "server_full") == "서버가 가득 찼습니다.");
  CHECK(text(Failure::ServerError, "no_public_rooms") == "공개된 구름을 찾을 수 없어요.");
  CHECK(text(Failure::ServerError, "bad_key") == "참가하지 못했습니다.");
  CHECK(text(Failure::RoomGone, "not_found") == "구름을 찾지 못했습니다.");
  CHECK(text(Failure::RoomClosed, "not_found", true) == "구름이 닫혀 다시 연결하지 못했어요.");
  CHECK(text(Failure::GraceExpired, {}, true) == "서버 연결이 종료되었습니다.");
  CHECK(text(Failure::Unauthorized) == "로그인 인증이 만료되어 구름에 연결할 수 없습니다.");
  CHECK(text(Failure::Unauthorized, {}, true) == "로그인 인증이 만료되어 구름에 다시 연결할 수 없습니다.");
  CHECK(text(Failure::ConnectFailed) == "서버에 연결하지 못했습니다.");
  CHECK(text(Failure::ConnectFailed, session::detail_closed) == "구름 서버 연결 실패: 연결이 종료되었습니다.");
  CHECK(text(Failure::ConnectFailed, session::detail_no_reply) == "서버가 구름 생성 응답을 보내지 않았습니다.");
  CHECK(text(Failure::InvalidArgument, "code") == "구름을 찾지 못했습니다.");
  CHECK(text(Failure::InvalidArgument, "resume") == "참가하지 못했습니다.");
}

void startup_waits_for_the_server() {
  Rig r;
  int started = 0;
  r.feedback->check_server([&] { ++started; });
  CHECK(r.probe.asked == 1);
  CHECK(started == 0);
  CHECK(!r.popup_drawn());

  r.probe.answer(true);
  CHECK(started == 1);
  CHECK(!r.popup_drawn());
  CHECK(r.h.app.wake_at() == 0);  // all is well: nothing is scheduled
}

void an_absent_server_is_retried_until_it_answers() {
  Rig r;
  int started = 0;
  r.feedback->check_server([&] { ++started; });
  r.probe.answer(false);
  CHECK(started == 0);
  CHECK(r.popup_drawn());
  CHECK(r.h.app.wake_at() != 0);
  CHECK(!r.probe.waiting());

  r.wake();
  CHECK(r.probe.asked == 2);
  CHECK(r.h.app.wake_at() == 0);  // none while a question is out
  r.probe.answer(false);
  CHECK(r.popup_drawn());
  CHECK(r.h.app.wake_at() != 0);

  r.wake();
  CHECK(r.probe.asked == 3);
  r.probe.answer(true);
  CHECK(started == 1);
  CHECK(!r.popup_drawn());
  CHECK(r.h.app.wake_at() == 0);
}

void the_popup_cannot_be_dismissed() {
  Rig r;
  r.go_offline();
  CHECK(r.popup_drawn());
  r.h.key(VK_ESCAPE);
  CHECK(r.popup_drawn());
  CHECK(r.h.click(r.popup(), OfflinePopup::Backdrop));
  CHECK(r.popup_drawn());
  CHECK(r.h.click(r.popup(), OfflinePopup::Card));
  CHECK(r.popup_drawn());
}

void the_popup_blocks_what_is_under_it() {
  Rig r;
  int created = 0;
  r.h.app.home().on_create = [&] { ++created; };
  r.go_offline();
  r.h.render();
  r.h.app.mouse_down(180, 260, false);  // the create cloud
  CHECK(created == 0);
}

void settings_cover_the_popup_and_stay_reachable() {
  Rig r;
  r.go_offline();
  CHECK(r.popup_drawn());
  CHECK(r.h.click(r.h.app.settings(), SettingsPanel::Droplet));  // above the backdrop
  CHECK(r.h.app.settings().is_open());
  CHECK(!r.popup_drawn());
  // Retrying goes on behind the settings.
  r.wake();
  CHECK(r.probe.asked == 2);
  r.probe.answer(false);
  CHECK(r.h.click(r.h.app.settings(), SettingsPanel::Droplet));
  CHECK(!r.h.app.settings().is_open());
  CHECK(r.popup_drawn());
}

void a_failed_connection_asks_before_blaming() {
  {
    // The server is gone: the popup says it all.
    Rig r;
    r.feedback->state_changed(State::Connecting);
    r.feedback->failed(Failure::ConnectFailed, {}, false);
    r.feedback->state_changed(State::Idle);
    CHECK(r.probe.waiting());
    CHECK(!r.popup_drawn());
    r.probe.answer(false);
    CHECK(r.popup_drawn());
  }
  {
    // The server is there: only this connection failed.
    Rig r;
    r.feedback->state_changed(State::Connecting);
    r.feedback->failed(Failure::ConnectFailed, {}, false);
    r.probe.answer(true);
    r.feedback->state_changed(State::Idle);
    CHECK(!r.popup_drawn());
    CHECK(r.h.app.wake_at() != 0);  // the toast is counting down
  }
  {
    // The server answered at the transport level, so nothing to ask.
    Rig r;
    r.feedback->failed(Failure::ServerError, "room_full", false);
    CHECK(r.probe.asked == 0);
    CHECK(!r.popup_drawn());
  }
}

void a_lost_room_shows_the_popup_until_it_is_back() {
  Rig r;
  r.h.app.show_session("ABC234");
  r.h.app.dialogs().show({"떠날까요?", "떠나기", "취소", false, nullptr, nullptr});
  r.feedback->state_changed(State::InRoom);
  CHECK(!r.popup_drawn());
  r.h.app.room().member_menu().open(1, false, 100, 10, 10);

  r.feedback->state_changed(State::Waiting);
  CHECK(r.popup_drawn());
  CHECK(!r.h.app.dialogs().is_open());
  CHECK(!r.h.app.room().member_menu().is_open());
  CHECK(r.h.app.view() == View::Session);  // the Room stays on screen
  CHECK(r.probe.asked == 0);               // Session does its own retrying
  CHECK(r.h.app.wake_at() == 0);

  r.feedback->state_changed(State::Connecting);
  r.feedback->state_changed(State::Joining);
  r.feedback->state_changed(State::Waiting);
  CHECK(r.popup_drawn());
  r.feedback->state_changed(State::Connecting);
  r.feedback->state_changed(State::Joining);
  r.feedback->state_changed(State::InRoom);
  CHECK(!r.popup_drawn());
}

void a_reconnect_that_ends_hides_the_popup_and_says_why() {
  Rig r;
  r.h.app.show_session("ABC234");
  r.feedback->state_changed(State::InRoom);
  r.feedback->state_changed(State::Waiting);
  CHECK(r.popup_drawn());
  r.h.app.show_home();
  r.feedback->failed(Failure::RoomClosed, "not_found", true);
  r.feedback->state_changed(State::Idle);
  CHECK(!r.popup_drawn());
  CHECK(r.probe.asked == 0);
  CHECK(r.h.app.wake_at() != 0);  // the toast
}

void a_room_entered_meanwhile_ends_the_question() {
  Rig r;
  int started = 0;
  r.feedback->check_server([&] { ++started; });
  r.feedback->state_changed(State::Connecting);
  r.probe.answer(false);
  CHECK(!r.popup_drawn());  // that connection will report for itself
  CHECK(started == 0);
  CHECK(r.h.app.wake_at() == 0);
}

void a_network_failure_elsewhere_asks_once() {
  Rig r;
  r.feedback->server_unreachable();
  r.feedback->server_unreachable();
  CHECK(r.probe.asked == 1);
  r.probe.answer(false);
  CHECK(r.popup_drawn());
  r.feedback->server_unreachable();
  CHECK(r.probe.asked == 1);
}

}  // namespace

int main() {
  failure_wording_is_upstreams();
  startup_waits_for_the_server();
  an_absent_server_is_retried_until_it_answers();
  the_popup_cannot_be_dismissed();
  the_popup_blocks_what_is_under_it();
  settings_cover_the_popup_and_stay_reachable();
  a_failed_connection_asks_before_blaming();
  a_lost_room_shows_the_popup_until_it_is_back();
  a_reconnect_that_ends_hides_the_popup_and_says_why();
  a_room_entered_meanwhile_ends_the_question();
  a_network_failure_elsewhere_asks_once();
  return crpp_test_result();
}
