#include "crpp/session/session.hpp"

#include <algorithm>
#include <initializer_list>
#include <type_traits>

namespace crpp::session {

void Session::SoundField::set(const std::optional<std::string_view>& v) noexcept {
  // A value the server would reject (longer than max_sound) is the same as none.
  present = v.has_value() && text.assign(*v);
  if (!present) text.clear();
}

std::optional<std::string_view> Session::SoundField::get() const noexcept {
  if (!present) return std::nullopt;
  return text.view();
}

// ---------------------------------------------------------------- plumbing

void Session::push(Action a) noexcept {
  // max_actions covers the worst case of any single call; see the tests.
  if (count_ < out_.size()) out_[count_++] = a;
}

void Session::set_state(State s) noexcept {
  if (state_ == s) return;
  state_ = s;
  push(action::Status{s});
}

void Session::close_transport() noexcept {
  if (!transport_) return;
  transport_ = false;
  push(action::Close{});
}

void Session::reset_room() noexcept {
  code_.clear();
  key_.clear();
  ticket_.clear();
  intent_ = Intent::Join;
  fallback_to_join_ = false;
  got_joined_ = false;
  got_resume_ = false;
  established_ = false;
  has_last_input_ = false;
}

void Session::fail(Failure reason, std::string_view detail) noexcept {
  close_transport();
  reset_room();
  push(action::Failed{reason, detail});
  set_state(State::Idle);
}

void Session::finish_leave() noexcept {
  close_transport();
  reset_room();
  set_state(State::Idle);
}

void Session::begin_connect(Time now) noexcept {
  got_joined_ = false;
  got_resume_ = false;
  has_last_input_ = false;
  transport_ = true;
  connect_deadline_ = now + timing_.connect_timeout;
  set_state(State::Connecting);
  push(action::Connect{});
}

void Session::become_ready(Time now) noexcept {
  established_ = true;
  next_ping_ = after(now, timing_.ping_interval);
  touch(now);
  set_state(State::InRoom);
}

// The connection dropped or timed out. An established room is re-entered after
// a delay; anything else is reported to the user.
void Session::lost(Time now, bool close_needed) noexcept {
  if (close_needed) {
    close_transport();
  } else {
    transport_ = false;
  }
  if (established_ && !key_.empty()) {
    retry_at_ = now + timing_.reconnect_delay;
    set_state(State::Waiting);
  } else {
    fail(Failure::ConnectFailed, {});
  }
}

wire::cmd::Sounds Session::sounds() const noexcept {
  return wire::cmd::Sounds{key_sound_.get(), mouse_sound_.get(), pen_sound_.get(),
                           emoji_sound_.get()};
}

wire::ClientMessage Session::control() const noexcept {
  std::optional<std::string_view> gid;
  if (!gid_.empty()) gid = gid_.view();
  switch (intent_) {
    case Intent::Create:
      return wire::cmd::Create{create_public_, create_chat_, gid};
    case Intent::JoinRandom:
      return wire::cmd::JoinRandom{gid};
    case Intent::Resume: {
      std::optional<std::string_view> ticket;
      if (!ticket_.empty()) ticket = ticket_.view();
      return wire::cmd::Resume{code_.view(), key_.view(), ticket, gid};
    }
    case Intent::Join:
      break;
  }
  return wire::cmd::Join{code_.view(), gid};
}

// ---------------------------------------------------------------- entry

std::span<const Action> Session::on(const Event& event, Time now) noexcept {
  count_ = 0;
  std::visit([this, now](const auto& e) { handle(e, now); }, event);
  return {out_.data(), count_};
}

bool Session::reconnecting() const noexcept {
  return established_ && (state_ == State::Waiting || state_ == State::Connecting ||
                          state_ == State::Joining);
}

Persistent Session::persistent() const noexcept {
  return Persistent{code_.view(), key_.view(), ticket_.view(), gid_.view()};
}

std::optional<Time> Session::deadline() const noexcept {
  // A disabled timer is Time::max() and never makes a deadline.
  const auto earliest = [](std::initializer_list<Time> times) -> std::optional<Time> {
    const Time t = *std::min_element(times.begin(), times.end());
    if (t == Time::max()) return std::nullopt;
    return t;
  };
  switch (state_) {
    case State::Idle: return std::nullopt;
    case State::Connecting: return connect_deadline_;
    case State::Joining: return join_deadline_;
    case State::InRoom: return earliest({next_ping_, liveness_});
    case State::Leaving: return earliest({next_ping_, liveness_, leave_deadline_});
    case State::Waiting: return retry_at_;
  }
  return std::nullopt;
}

// ---------------------------------------------------------------- commands

void Session::handle(const event::Create& e, Time now) {
  if (state_ != State::Idle) return;
  reset_room();
  intent_ = Intent::Create;
  create_public_ = e.is_public;
  create_chat_ = e.chat;
  begin_connect(now);
}

void Session::handle(const event::Join& e, Time now) {
  if (state_ != State::Idle) return;
  reset_room();
  if (e.code.empty() || !code_.assign(e.code)) {
    reset_room();
    push(action::Failed{Failure::InvalidArgument, "code"});
    return;
  }
  intent_ = Intent::Join;
  begin_connect(now);
}

void Session::handle(const event::JoinRandom&, Time now) {
  if (state_ != State::Idle) return;
  reset_room();
  intent_ = Intent::JoinRandom;
  begin_connect(now);
}

void Session::handle(const event::Resume& e, Time now) {
  if (state_ != State::Idle) return;
  reset_room();
  const bool ok = !e.code.empty() && code_.assign(e.code) && !e.key.empty() &&
                  key_.assign(e.key) && (!e.ticket || ticket_.assign(*e.ticket));
  if (!ok) {
    reset_room();
    push(action::Failed{Failure::InvalidArgument, "resume"});
    return;
  }
  intent_ = Intent::Resume;
  fallback_to_join_ = e.fallback_to_join;
  begin_connect(now);
}

void Session::handle(const event::Leave&, Time now) {
  switch (state_) {
    case State::InRoom:
      push(action::Send{wire::cmd::Leave{}});
      leave_deadline_ = now + timing_.leave_timeout;
      set_state(State::Leaving);
      return;
    case State::Connecting:
    case State::Joining:
      close_transport();
      reset_room();
      set_state(State::Idle);
      return;
    case State::Waiting:
      reset_room();
      set_state(State::Idle);
      return;
    case State::Idle:
    case State::Leaving:
      return;
  }
}

void Session::handle(const event::SetSounds& e, Time) {
  key_sound_.set(e.key);
  mouse_sound_.set(e.mouse);
  pen_sound_.set(e.pen);
  emoji_sound_.set(e.emoji);
  if (state_ == State::InRoom) push(action::Send{sounds()});
}

void Session::handle(const event::Input& e, Time now) {
  if (state_ != State::InRoom || !wire::is_known(e.kind)) return;
  // One event per interval, the rest dropped: the server rate-limits each
  // member to 10 events/s and blocks sustained bursts.
  if (has_last_input_ && now - last_input_ < timing_.input_interval) return;
  has_last_input_ = true;
  last_input_ = now;
  push(action::SendInput{e.kind});
}

void Session::handle(const event::Forward& e, Time) {
  if (state_ != State::InRoom) return;
  const bool allowed = std::visit(
      [](const auto& m) {
        using T = std::decay_t<decltype(m)>;
        return std::is_same_v<T, wire::cmd::Chat> || std::is_same_v<T, wire::cmd::Profile> ||
               std::is_same_v<T, wire::cmd::Nick> || std::is_same_v<T, wire::cmd::Visibility> ||
               std::is_same_v<T, wire::cmd::ChatEnable> ||
               std::is_same_v<T, wire::cmd::ChatDisable>;
      },
      e.message);
  if (allowed) push(action::Send{e.message});
}

// ---------------------------------------------------------------- transport

void Session::handle(const event::Connected&, Time now) {
  if (state_ != State::Connecting) return;
  join_deadline_ = now + timing_.join_timeout;
  touch(now);
  set_state(State::Joining);
  // Same order as upstream: sounds first, then exactly one control message.
  push(action::Send{sounds()});
  push(action::Send{control()});
}

void Session::handle(const event::ConnectFailed& e, Time now) {
  if (state_ != State::Connecting) return;
  transport_ = false;
  if (e.unauthorized) {
    fail(Failure::Unauthorized, {});
    return;
  }
  lost(now, false);
}

void Session::handle(const event::Closed&, Time now) {
  switch (state_) {
    case State::Connecting:
    case State::Joining:
    case State::InRoom:
      lost(now, false);
      return;
    case State::Leaving:
      transport_ = false;
      finish_leave();
      return;
    case State::Idle:
    case State::Waiting:
      return;
  }
}

void Session::handle(const event::Heard&, Time now) {
  if (transport_) touch(now);
}

void Session::handle(const event::Binary& e, Time now) {
  if (state_ != State::InRoom) return;
  touch(now);
  const auto n = wire::decode_server_frame(e.bytes, peer_events_);
  if (!n) return;
  push(action::PeerInput{std::span<const wire::ServerEvent>(peer_events_.data(), *n)});
}

void Session::handle(const event::Text& e, Time now) {
  if (state_ != State::Joining && state_ != State::InRoom && state_ != State::Leaving) return;
  touch(now);
  if (e.json.size() > scratch_.size()) return;
  const auto parsed = wire::parse_server_message(
      e.json, wire::ParseBuffers{std::span<char>(scratch_.data(), e.json.size()), members_});
  if (!parsed) return;  // malformed messages are ignored, as upstream does
  on_server_message(*parsed, now);
}

void Session::handle(const event::Tick&, Time now) {
  switch (state_) {
    case State::Idle:
      return;
    case State::Connecting:
      if (now >= connect_deadline_) lost(now, true);
      return;
    case State::Joining:
      if (now >= join_deadline_) lost(now, true);
      return;
    case State::Waiting:
      if (now >= retry_at_) {
        intent_ = Intent::Resume;
        fallback_to_join_ = false;
        begin_connect(now);
      }
      return;
    case State::InRoom:
    case State::Leaving:
      if (state_ == State::Leaving && now >= leave_deadline_) {
        finish_leave();
        return;
      }
      if (now >= liveness_) {
        lost(now, true);
        return;
      }
      if (now >= next_ping_) {
        push(action::Ping{});
        next_ping_ = after(now, timing_.ping_interval);
      }
      return;
  }
}

// ---------------------------------------------------------------- server

void Session::on_server_message(const wire::ServerMessage& m, Time now) {
  using namespace wire;
  if (const auto* gid = std::get_if<msg::Gid>(&m)) {
    if (gid_.assign(gid->gid)) push(action::Persist{});
  } else if (const auto* joined = std::get_if<msg::Joined>(&m)) {
    if (state_ != State::Joining) return;
    if (!code_.assign(joined->code)) {
      fail(Failure::ServerError, "bad_code");
      return;
    }
    got_joined_ = true;
    push(action::Deliver{m});
    push(action::Persist{});
    if (got_resume_) become_ready(now);
  } else if (const auto* resume = std::get_if<msg::Resume>(&m)) {
    if (state_ != State::Joining && state_ != State::InRoom) return;
    if (!key_.assign(resume->key) || !code_.assign(resume->code)) {
      fail(Failure::ServerError, "bad_key");
      return;
    }
    got_resume_ = true;
    push(action::Persist{});
    if (state_ == State::Joining && got_joined_) become_ready(now);
  } else if (const auto* ticket = std::get_if<msg::Ticket>(&m)) {
    if (state_ == State::Joining || state_ == State::InRoom) {
      if (ticket_.assign(ticket->ticket)) push(action::Persist{});
    }
  } else if (std::holds_alternative<msg::Members>(m) || std::holds_alternative<msg::Chat>(m) ||
             std::holds_alternative<msg::ChatBlocked>(m) ||
             std::holds_alternative<msg::Profile>(m)) {
    if (got_joined_ || state_ == State::InRoom) push(action::Deliver{m});
  } else if (std::holds_alternative<msg::Left>(m)) {
    if (state_ == State::Leaving || state_ == State::InRoom) finish_leave();
  } else if (const auto* error = std::get_if<msg::Error>(&m)) {
    on_server_error(error->error, now);
  }
}

void Session::on_server_error(std::string_view detail, Time now) {
  if (detail == "not_found" && intent_ == Intent::Resume && state_ == State::Joining) {
    if (fallback_to_join_ && !code_.empty()) {
      // The held seat is gone: join the same room normally on a new connection.
      close_transport();
      key_.clear();
      ticket_.clear();
      intent_ = Intent::Join;
      fallback_to_join_ = false;
      begin_connect(now);
      return;
    }
    fail(Failure::RoomGone, detail);
    return;
  }
  fail(Failure::ServerError, detail);
}

}  // namespace crpp::session
