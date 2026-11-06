#include "crpp/ui/connection_feedback.hpp"

#include <windows.h>

#include <utility>

namespace crpp::ui {

namespace {

std::string_view server_error_text(std::string_view code) {
  if (code == "not_found") return "구름을 찾지 못했습니다.";
  if (code == "room_full") return "구름이 가득 찼습니다.";
  if (code == "already_in_room") return "이미 구름에 참가 중입니다.";
  if (code == "server_full") return "서버가 가득 찼습니다.";
  if (code == "no_public_rooms") return "공개된 구름을 찾을 수 없어요.";
  return "참가하지 못했습니다.";
}

}  // namespace

std::string_view failure_text(session::Failure reason, std::string_view detail, bool in_room) {
  using session::Failure;
  switch (reason) {
    case Failure::Unauthorized:
      return in_room ? "로그인 인증이 만료되어 구름에 다시 연결할 수 없습니다."
                     : "로그인 인증이 만료되어 구름에 연결할 수 없습니다.";
    case Failure::ConnectFailed:
      if (detail == session::detail_closed) return "구름 서버 연결 실패: 연결이 종료되었습니다.";
      if (detail == session::detail_no_reply) return "서버가 구름 생성 응답을 보내지 않았습니다.";
      return "서버에 연결하지 못했습니다.";
    case Failure::RoomGone:
      return server_error_text("not_found");
    case Failure::ServerError:
      return server_error_text(detail);
    case Failure::InvalidArgument:
      return server_error_text(detail == "code" ? "not_found" : "");
    case Failure::RoomClosed:
      return "구름이 닫혀 다시 연결하지 못했어요.";
    case Failure::GraceExpired:
      return "서버 연결이 종료되었습니다.";
  }
  return server_error_text("");
}

ConnectionFeedback::ConnectionFeedback(App& app, HealthProbe& probe) : app_(app), probe_(probe) {
  app_.offline().on_retry = [this] { ask(); };
}

void ConnectionFeedback::check_server(std::function<void()> then) {
  then_ = std::move(then);
  ask();
}

void ConnectionFeedback::server_unreachable() {
  if (asking_ || offline_ || state_ != session::State::Idle) return;
  ask();
}

void ConnectionFeedback::ask() {
  if (asking_) return;
  asking_ = true;
  probe_.start([this](bool reachable) { answered(reachable); });
}

void ConnectionFeedback::answered(bool reachable) {
  asking_ = false;
  std::string pending = std::exchange(pending_, {});
  auto then = std::exchange(then_, nullptr);
  if (state_ != session::State::Idle) {
    // A connection took over while the question was out, and reports for itself.
    if (offline_) app_.offline().hide();
    offline_ = false;
    return;
  }
  if (!reachable) {
    then_ = std::move(then);
    offline_ = true;
    app_.offline().show();
    app_.offline().retry_at(GetTickCount64() + retry_ms);
    return;
  }
  if (offline_) app_.offline().hide();
  offline_ = false;
  if (!pending.empty()) tell(pending, false);
  if (then) then();
}

void ConnectionFeedback::tell(std::string_view text, bool in_room) {
  // Back from a Room, no button on Home has anything to do with this.
  if (!in_room && app_.view() == View::Home) app_.home().show_error(std::string(text));
  else app_.toasts().toast(std::string(text));
}

void ConnectionFeedback::state_changed(session::State state) {
  state_ = state;
  if (state == session::State::Waiting && !reconnecting_) {
    reconnecting_ = true;
    // What was open in the Room no longer means anything. Settings stay as they are.
    if (app_.view() == View::Session && !app_.settings().is_open()) app_.dialogs().close();
    app_.room().close_member_popups();
    app_.offline().show();
  } else if (state == session::State::InRoom || (state == session::State::Idle && reconnecting_)) {
    reconnecting_ = false;
    offline_ = false;
    app_.offline().hide();
  }
}

void ConnectionFeedback::failed(session::Failure reason, std::string_view detail, bool in_room) {
  state_ = session::State::Idle;  // Session goes there right after reporting
  if (reconnecting_) {
    reconnecting_ = false;
    app_.offline().hide();
  }
  const std::string_view text = failure_text(reason, detail, in_room);
  if (reason == session::Failure::ConnectFailed && detail.empty()) {
    pending_ = text;
    ask();
    return;
  }
  tell(text, in_room);
}

}  // namespace crpp::ui
