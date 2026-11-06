#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "check.hpp"
#include "crpp/session/session.hpp"

using namespace crpp::session;
using namespace crpp;
using namespace std::chrono_literals;
using namespace std::string_view_literals;

namespace {

constexpr auto joined_json =
    R"({"t":"joined","code":"ABC234","me":1,"members":[{"slot":0,"order":1,"offline":false,"nick":"a","cid":"c0"}],"host":0,"public":false,"chat":false})"sv;
constexpr auto resume_json = R"({"t":"resume","code":"ABC234","key":"K1"})"sv;
constexpr auto ticket_json = R"({"t":"ticket","code":"ABC234","ticket":"T1"})"sv;
constexpr auto gid_json = R"({"t":"gid","gid":"G1"})"sv;

using Actions = std::span<const Action>;

template <class T>
const T* find(Actions a) {
  for (const Action& x : a) {
    if (const auto* p = std::get_if<T>(&x)) return p;
  }
  return nullptr;
}

template <class T>
std::size_t count(Actions a) {
  std::size_t n = 0;
  for (const Action& x : a) n += std::holds_alternative<T>(x) ? 1 : 0;
  return n;
}

std::string json(const wire::ClientMessage& m) {
  std::array<char, 2048> out{};
  const auto n = wire::encode_client_message(m, out);
  return n ? std::string(out.data(), *n) : std::string("<encode error>");
}

// Text of every Send action, in order.
std::vector<std::string> sent(Actions a) {
  std::vector<std::string> r;
  for (const Action& x : a) {
    if (const auto* s = std::get_if<action::Send>(&x)) r.push_back(json(s->message));
  }
  return r;
}

bool states_are(Actions a, std::initializer_list<State> expected) {
  std::vector<State> got;
  for (const Action& x : a) {
    if (const auto* s = std::get_if<action::Status>(&x)) got.push_back(s->state);
  }
  return std::vector<State>(expected) == got;
}

struct Rig {
  std::unique_ptr<Session> session = std::make_unique<Session>();
  Time now{};
  Actions last;

  Actions step(const Event& e) {
    last = session->on(e, now);
    CHECK(last.size() < 8);  // headroom below the action buffer capacity
    return last;
  }
  Actions text(std::string_view j) { return step(event::Text{j}); }
  Actions advance(std::chrono::milliseconds d) {
    now += d;
    return step(event::Tick{});
  }
  Session& s() { return *session; }
};

// Create -> connected -> gid, joined, resume, ticket. Leaves the rig InRoom.
void enter_room(Rig& r) {
  (void)r.step(event::Create{true, false});
  (void)r.step(event::Connected{});
  (void)r.text(gid_json);
  (void)r.text(joined_json);
  (void)r.text(resume_json);
  (void)r.text(ticket_json);
}

void create_flow() {
  Rig r;
  auto a = r.step(event::Create{true, false});
  CHECK(states_are(a, {State::Connecting}));
  CHECK(count<action::Connect>(a) == 1);
  CHECK(r.s().deadline() == r.now + 15s);

  a = r.step(event::Connected{});
  CHECK(states_are(a, {State::Joining}));
  const auto s = sent(a);
  CHECK(s.size() == 2);
  CHECK(s.size() == 2 && s[0] == R"({"t":"sounds"})");
  CHECK(s.size() == 2 && s[1] == R"({"t":"create","public":true,"chat":false})");
  CHECK(r.s().deadline() == r.now + 15s);

  a = r.text(gid_json);
  CHECK(count<action::Persist>(a) == 1);
  CHECK(r.s().persistent().gid == "G1");

  a = r.text(joined_json);
  CHECK(find<action::Deliver>(a) && std::holds_alternative<wire::msg::Joined>(find<action::Deliver>(a)->message));
  CHECK(r.s().state() == State::Joining);  // not ready until the resume key is known

  a = r.text(resume_json);
  CHECK(states_are(a, {State::InRoom}));
  CHECK(r.s().persistent().code == "ABC234");
  CHECK(r.s().persistent().key == "K1");

  a = r.text(ticket_json);
  CHECK(count<action::Persist>(a) == 1);
  CHECK(r.s().persistent().ticket == "T1");
}

void resume_before_joined_still_waits_for_both() {
  Rig r;
  (void)r.step(event::JoinRandom{});
  (void)r.step(event::Connected{});
  (void)r.text(resume_json);
  CHECK(r.s().state() == State::Joining);
  const auto a = r.text(joined_json);
  CHECK(states_are(a, {State::InRoom}));
}

void guest_id_is_reused() {
  Rig r;
  enter_room(r);
  (void)r.step(event::Leave{});
  (void)r.text(R"({"t":"left"})"sv);
  CHECK(r.s().state() == State::Idle);
  CHECK(r.s().persistent().code.empty());
  CHECK(r.s().persistent().gid == "G1");  // survives leaving

  (void)r.step(event::Join{"XYZ789"});
  const auto a = r.step(event::Connected{});
  const auto s = sent(a);
  CHECK(s.size() == 2 && s[1] == R"({"t":"join","code":"XYZ789","gid":"G1"})");
}

void sounds_cache() {
  Rig r;
  CHECK(r.step(event::SetSounds{"01_Plastic"sv, std::nullopt, std::nullopt, "1f4a7"sv}).empty());
  (void)r.step(event::JoinRandom{});
  auto s = sent(r.step(event::Connected{}));
  CHECK(s.size() == 2 && s[0] == R"({"t":"sounds","key":"01_Plastic","emoji":"1f4a7"})");
  CHECK(s.size() == 2 && s[1] == R"({"t":"join_random"})");

  (void)r.text(joined_json);
  (void)r.text(resume_json);
  s = sent(r.step(event::SetSounds{"02_Thocky"sv, std::nullopt, std::nullopt, std::nullopt}));
  CHECK(s.size() == 1 && s[0] == R"({"t":"sounds","key":"02_Thocky"})");
}

void input_throttle() {
  Rig r;
  CHECK(r.step(event::Input{wire::Kind::KeyNormal}).empty());  // not in a room yet
  enter_room(r);

  auto a = r.step(event::Input{wire::Kind::KeyNormal});
  CHECK(count<action::SendInput>(a) == 1);
  r.now += 50ms;
  CHECK(r.step(event::Input{wire::Kind::KeySpace}).empty());  // inside the interval: dropped
  r.now += 50ms;
  a = r.step(event::Input{wire::Kind::KeySpace});
  CHECK(count<action::SendInput>(a) == 1);
  CHECK(find<action::SendInput>(a) && find<action::SendInput>(a)->kind == wire::Kind::KeySpace);

  r.now += 1s;
  CHECK(r.step(event::Input{static_cast<wire::Kind>(6)}).empty());  // the server would drop it
  CHECK(r.step(event::Input{static_cast<wire::Kind>(11)}).size() == 1);
}

void peer_input() {
  Rig r;
  enter_room(r);
  const std::array<std::uint8_t, 4> frame{3, 16, 0, 44};
  const auto a = r.step(event::Binary{frame});
  const auto* p = find<action::PeerInput>(a);
  CHECK(p != nullptr);
  if (p) {
    CHECK(p->events.size() == 2);
    CHECK(p->events.size() == 2 && p->events[0].slot == 3 && p->events[0].kind == wire::Kind::KeyNormal);
  }
  const std::array<std::uint8_t, 3> bad{1, 2, 3};
  CHECK(r.step(event::Binary{bad}).empty());
}

void ping_and_reconnect() {
  Rig r;
  enter_room(r);
  const Time entered = r.now;

  CHECK(r.s().deadline() == entered + 20s);
  CHECK(r.advance(19s).empty());
  auto a = r.advance(1s);
  CHECK(count<action::Ping>(a) == 1);
  CHECK(r.s().deadline() == entered + 40s);
  CHECK(count<action::Ping>(r.advance(20s)) == 1);
  CHECK(count<action::Ping>(r.advance(15s)) == 0);  // t = 55 s, next ping at 60 s

  // A frame heard at 55 s pushes the liveness limit to 130 s.
  (void)r.step(event::Heard{});
  CHECK(r.advance(5s).size() == 1);  // ping at 60 s
  r.now = entered + 129s;
  CHECK(count<action::Close>(r.step(event::Tick{})) == 0);
  a = r.advance(1s);  // 130 s: silent for 75 s
  CHECK(count<action::Close>(a) == 1);
  CHECK(states_are(a, {State::Waiting}));
  CHECK(r.s().reconnecting());
  CHECK(r.s().deadline() == r.now + 1s);

  CHECK(r.advance(500ms).empty());
  a = r.advance(500ms);
  CHECK(states_are(a, {State::Connecting}));
  CHECK(count<action::Connect>(a) == 1);

  a = r.step(event::Connected{});
  const auto s = sent(a);
  CHECK(s.size() == 2);
  CHECK(s.size() == 2 &&
        s[1] == R"({"t":"resume","code":"ABC234","key":"K1","ticket":"T1","gid":"G1"})");
  CHECK(r.s().reconnecting());

  (void)r.text(joined_json);
  a = r.text(R"({"t":"resume","code":"ABC234","key":"K2"})"sv);
  CHECK(states_are(a, {State::InRoom}));
  CHECK(!r.s().reconnecting());
  CHECK(r.s().persistent().key == "K2");
}

void disabled_keepalive_timers() {
  Rig r;
  r.session = std::make_unique<Session>(Timing{.ping_interval = 0ms, .liveness_timeout = 0ms});
  enter_room(r);
  CHECK(!r.s().deadline());
  for (int i = 0; i < 100; ++i) CHECK(r.advance(60s).empty());
  CHECK(r.s().state() == State::InRoom);
  // Leaving still has its own deadline.
  (void)r.step(event::Leave{});
  CHECK(r.s().deadline() == r.now + 5s);
}

void repeated_reconnect_failures_keep_retrying() {
  Rig r;
  enter_room(r);
  (void)r.step(event::Closed{});
  CHECK(r.s().state() == State::Waiting);
  for (int i = 0; i < 5; ++i) {
    (void)r.advance(30s);
    CHECK(r.s().state() == State::Connecting);
    const auto a = r.step(event::ConnectFailed{false});
    CHECK(r.s().state() == State::Waiting);
    CHECK(find<action::Failed>(a) == nullptr);
  }
  CHECK(r.s().persistent().key == "K1");
}

// Takes a Rig in Waiting through one failed attempt; the wait that follows it.
std::chrono::milliseconds fail_one_attempt(Rig& r) {
  r.now = *r.s().deadline();
  (void)r.step(event::Tick{});
  CHECK(r.s().state() == State::Connecting);
  (void)r.step(event::ConnectFailed{false});
  CHECK(r.s().state() == State::Waiting);
  return std::chrono::duration_cast<std::chrono::milliseconds>(*r.s().deadline() - r.now);
}

void backoff_doubles_up_to_the_longest_wait() {
  Rig r;
  enter_room(r);
  (void)r.step(event::Closed{});
  CHECK(r.s().deadline() == r.now + 1s);
  for (const std::chrono::seconds expected : {2s, 4s, 8s, 16s, 30s, 30s, 30s}) CHECK(fail_one_attempt(r) == expected);
}

void backoff_starts_over_after_a_reconnect() {
  Rig r;
  enter_room(r);
  (void)r.step(event::Closed{});
  CHECK(fail_one_attempt(r) == 2s);
  CHECK(fail_one_attempt(r) == 4s);
  r.now = *r.s().deadline();
  (void)r.step(event::Tick{});
  (void)r.step(event::Connected{});
  (void)r.text(joined_json);
  (void)r.text(resume_json);
  CHECK(r.s().state() == State::InRoom);
  (void)r.step(event::Closed{});
  CHECK(r.s().deadline() == r.now + 1s);
}

void jitter_cuts_each_wait_by_up_to_half() {
  double fraction = 0;
  const Jitter jitter{[](void* context) noexcept { return *static_cast<double*>(context); }, &fraction};
  using Waits = std::pair<std::chrono::milliseconds, std::chrono::milliseconds>;
  const auto waits_with = [&](double f) {
    fraction = f;
    Rig r;
    r.session = std::make_unique<Session>(Timing{}, jitter);
    enter_room(r);
    (void)r.step(event::Closed{});
    const auto first = std::chrono::duration_cast<std::chrono::milliseconds>(*r.s().deadline() - r.now);
    return Waits{first, fail_one_attempt(r)};
  };
  CHECK((waits_with(0) == Waits{500ms, 1000ms}));
  CHECK((waits_with(0.5) == Waits{750ms, 1500ms}));
  // A source that breaks its promise cannot stretch the wait or make it negative.
  CHECK((waits_with(-5) == Waits{500ms, 1000ms}));
  CHECK((waits_with(7) == Waits{1000ms, 2000ms}));
}

void reconnecting_stops_at_the_servers_grace() {
  Rig r;
  enter_room(r);
  (void)r.step(event::Closed{});
  const Time limit = r.now + 15min;
  while (*r.s().deadline() < limit) (void)fail_one_attempt(r);
  CHECK(r.s().deadline() == limit);  // the last wait is cut to end at the limit
  r.now = limit;
  const auto a = r.step(event::Tick{});
  const auto* f = find<action::Failed>(a);
  CHECK(f && f->reason == Failure::GraceExpired);
  CHECK(states_are(a, {State::Idle}));
  CHECK(!r.s().deadline());
  CHECK(r.s().persistent().key.empty());
}

void attempt_failing_after_the_grace_gives_up_at_once() {
  Rig r;
  enter_room(r);
  (void)r.step(event::Closed{});
  r.now = *r.s().deadline();
  (void)r.step(event::Tick{});
  r.now += 16min;  // the attempt hung until after the limit
  const auto a = r.step(event::ConnectFailed{false});
  const auto* f = find<action::Failed>(a);
  CHECK(f && f->reason == Failure::GraceExpired);
  CHECK(r.s().state() == State::Idle);
}

void reconnecting_stops_when_the_token_is_refused() {
  Rig r;
  enter_room(r);
  (void)r.step(event::Closed{});
  (void)r.advance(1s);
  const auto a = r.step(event::ConnectFailed{true});
  const auto* f = find<action::Failed>(a);
  CHECK(f && f->reason == Failure::Unauthorized);
  CHECK(r.s().state() == State::Idle);
}

void reconnecting_stops_when_the_room_is_closed() {
  for (const auto error : {R"({"t":"error","error":"not_found"})"sv, R"({"t":"error","error":"room_full"})"sv}) {
    Rig r;
    enter_room(r);
    (void)r.step(event::Closed{});
    (void)r.advance(1s);
    (void)r.step(event::Connected{});
    const auto a = r.text(error);
    const auto* f = find<action::Failed>(a);
    CHECK(f && f->reason == Failure::RoomClosed);
    CHECK(r.s().state() == State::Idle);
  }
}

void failures_say_how_far_the_connection_got() {
  const auto detail = [](Actions a) {
    const auto* f = find<action::Failed>(a);
    return f ? f->detail : "(none)"sv;
  };
  {
    Rig r;
    (void)r.step(event::Create{});
    CHECK(detail(r.step(event::ConnectFailed{false})).empty());
  }
  {
    Rig r;
    (void)r.step(event::Create{});
    CHECK(detail(r.advance(15s)).empty());
  }
  {
    Rig r;
    (void)r.step(event::Create{});
    (void)r.step(event::Connected{});
    CHECK(detail(r.step(event::Closed{})) == detail_closed);
  }
  {
    Rig r;
    (void)r.step(event::Create{});
    (void)r.step(event::Connected{});
    CHECK(detail(r.advance(15s)) == detail_no_reply);
  }
}

void initial_connection_failures() {
  {
    Rig r;
    (void)r.step(event::Create{});
    (void)r.advance(14s);
    const auto a = r.advance(1s);  // 15 s connect timeout
    CHECK(count<action::Close>(a) == 1);
    CHECK(find<action::Failed>(a) && find<action::Failed>(a)->reason == Failure::ConnectFailed);
    CHECK(r.s().state() == State::Idle && !r.s().deadline());
  }
  {
    Rig r;
    (void)r.step(event::Create{});
    const auto a = r.step(event::ConnectFailed{true});
    CHECK(find<action::Failed>(a) && find<action::Failed>(a)->reason == Failure::Unauthorized);
    CHECK(count<action::Close>(a) == 0);  // nothing was open
  }
  {
    Rig r;
    (void)r.step(event::Join{"ABC234"});
    (void)r.step(event::Connected{});
    const auto a = r.advance(15s);  // join timeout
    CHECK(count<action::Close>(a) == 1);
    CHECK(find<action::Failed>(a) && find<action::Failed>(a)->reason == Failure::ConnectFailed);
  }
  {
    Rig r;
    (void)r.step(event::Join{"ABC234"});
    (void)r.step(event::Connected{});
    const auto a = r.step(event::Closed{});
    CHECK(find<action::Failed>(a) && find<action::Failed>(a)->reason == Failure::ConnectFailed);
    CHECK(count<action::Close>(a) == 0);  // already closed by the peer
  }
}

void server_errors() {
  {
    Rig r;
    (void)r.step(event::Join{"ABC234"});
    (void)r.step(event::Connected{});
    const auto a = r.text(R"({"t":"error","error":"room_full"})"sv);
    const auto* f = find<action::Failed>(a);
    CHECK(f && f->reason == Failure::ServerError && f->detail == "room_full");
    CHECK(count<action::Close>(a) == 1);
    CHECK(r.s().state() == State::Idle);
  }
  {
    // Reconnect finds the room gone for good.
    Rig r;
    enter_room(r);
    (void)r.step(event::Closed{});
    (void)r.advance(1s);
    (void)r.step(event::Connected{});
    const auto a = r.text(R"({"t":"error","error":"not_found"})"sv);
    CHECK(find<action::Failed>(a) && find<action::Failed>(a)->reason == Failure::RoomClosed);
    CHECK(r.s().persistent().key.empty());
    CHECK(r.s().state() == State::Idle);
  }
}

void resume_with_fallback_to_join() {
  Rig r;
  auto a = r.step(event::Resume{"ABC234", "OLD", "TK"sv, true});
  CHECK(count<action::Connect>(a) == 1);
  auto s = sent(r.step(event::Connected{}));
  CHECK(s.size() == 2 && s[1] == R"({"t":"resume","code":"ABC234","key":"OLD","ticket":"TK"})");

  a = r.text(R"({"t":"error","error":"not_found"})"sv);
  CHECK(count<action::Close>(a) == 1);
  CHECK(count<action::Connect>(a) == 1);
  CHECK(find<action::Failed>(a) == nullptr);
  CHECK(r.s().state() == State::Connecting);

  s = sent(r.step(event::Connected{}));
  CHECK(s.size() == 2 && s[1] == R"({"t":"join","code":"ABC234"})");

  // Without the fallback the same error is final.
  Rig q;
  (void)q.step(event::Resume{"ABC234", "OLD", std::nullopt, false});
  (void)q.step(event::Connected{});
  a = q.text(R"({"t":"error","error":"not_found"})"sv);
  CHECK(find<action::Failed>(a) && find<action::Failed>(a)->reason == Failure::RoomGone);
}

void leaving() {
  {
    Rig r;
    enter_room(r);
    auto a = r.step(event::Leave{});
    CHECK(sent(a) == std::vector<std::string>{R"({"t":"leave"})"});
    CHECK(states_are(a, {State::Leaving}));
    CHECK(r.step(event::Input{wire::Kind::KeyNormal}).empty());
    a = r.text(R"({"t":"left"})"sv);
    CHECK(count<action::Close>(a) == 1);
    CHECK(states_are(a, {State::Idle}));
    CHECK(r.s().persistent().key.empty());
  }
  {
    Rig r;
    enter_room(r);
    (void)r.step(event::Leave{});
    const auto a = r.advance(5s);  // no reply from the server
    CHECK(count<action::Close>(a) == 1);
    CHECK(r.s().state() == State::Idle);
  }
  {
    Rig r;
    enter_room(r);
    (void)r.step(event::Leave{});
    const auto a = r.step(event::Closed{});  // the server closed first
    CHECK(count<action::Close>(a) == 0);
    CHECK(r.s().state() == State::Idle);
  }
  {
    Rig r;
    enter_room(r);
    (void)r.step(event::Closed{});
    CHECK(r.s().state() == State::Waiting);
    const auto a = r.step(event::Leave{});
    CHECK(states_are(a, {State::Idle}));
    CHECK(!r.s().deadline());
  }
  {
    Rig r;
    (void)r.step(event::Create{});
    const auto a = r.step(event::Leave{});  // cancel while connecting
    CHECK(count<action::Close>(a) == 1);
    CHECK(r.s().state() == State::Idle);
  }
}

void forward_and_commands_by_state() {
  Rig r;
  CHECK(r.step(event::Forward{wire::cmd::Chat{"hi"}}).empty());  // not in a room
  enter_room(r);

  auto s = sent(r.step(event::Forward{wire::cmd::Chat{"hi"}}));
  CHECK(s == std::vector<std::string>{R"({"t":"chat","text":"hi"})"});
  s = sent(r.step(event::Forward{wire::cmd::Profile{2}}));
  CHECK(s == std::vector<std::string>{R"({"t":"profile","slot":2})"});
  // Control messages are the session's business, not the caller's.
  CHECK(r.step(event::Forward{wire::cmd::Leave{}}).empty());
  CHECK(r.step(event::Forward{wire::cmd::Join{"ABC234", std::nullopt}}).empty());
  // A second Create while in a room is ignored.
  CHECK(r.step(event::Create{}).empty());
  CHECK(r.s().state() == State::InRoom);
}

void invalid_arguments() {
  Rig r;
  auto a = r.step(event::Join{"TOOLONGCODE"});
  CHECK(find<action::Failed>(a) && find<action::Failed>(a)->reason == Failure::InvalidArgument);
  CHECK(r.s().state() == State::Idle);
  a = r.step(event::Join{""});
  CHECK(find<action::Failed>(a) != nullptr);
  a = r.step(event::Resume{"ABC234", "", std::nullopt, false});
  CHECK(find<action::Failed>(a) != nullptr);
  const std::string long_key(200, 'k');
  a = r.step(event::Resume{"ABC234", long_key, std::nullopt, false});
  CHECK(find<action::Failed>(a) != nullptr);
  CHECK(r.s().state() == State::Idle);
}

void bad_server_input_is_ignored() {
  Rig r;
  enter_room(r);
  CHECK(r.text("not json"sv).empty());
  CHECK(r.text(R"({"t":"future"})"sv).empty());
  CHECK(r.text(R"({"t":"chat"})"sv).empty());  // missing fields
  const std::string huge(max_incoming_text + 1, ' ');
  CHECK(r.text(huge).empty());
  CHECK(r.s().state() == State::InRoom);
  // Frames of a closed or idle session are ignored.
  Rig idle;
  CHECK(idle.text(joined_json).empty());
  CHECK(idle.step(event::Closed{}).empty());
  CHECK(idle.step(event::Tick{}).empty());
}

void room_updates_are_delivered() {
  Rig r;
  enter_room(r);
  auto a = r.text(R"({"t":"members","members":[],"host":0,"public":true,"chat":true})"sv);
  CHECK(find<action::Deliver>(a) && std::holds_alternative<wire::msg::Members>(find<action::Deliver>(a)->message));
  a = r.text(R"({"t":"chat_blocked","ms":3000})"sv);
  CHECK(count<action::Deliver>(a) == 1);
  a = r.text(R"({"t":"chat","slot":0,"cid":"c","nick":"n","text":"yo","time":5})"sv);
  CHECK(count<action::Deliver>(a) == 1);
  a = r.text(R"({"t":"profile","slot":0,"nick":"n"})"sv);
  CHECK(count<action::Deliver>(a) == 1);
}

void oversized_values_are_rejected() {
  Rig r;
  (void)r.step(event::Join{"ABC234"});
  (void)r.step(event::Connected{});
  const std::string key(max_key + 1, 'k');
  const std::string j = R"({"t":"resume","code":"ABC234","key":")" + key + R"("})";
  const auto a = r.text(j);
  CHECK(find<action::Failed>(a) && find<action::Failed>(a)->reason == Failure::ServerError);
  CHECK(r.s().state() == State::Idle);
}

}  // namespace

int main() {
  create_flow();
  resume_before_joined_still_waits_for_both();
  guest_id_is_reused();
  sounds_cache();
  input_throttle();
  peer_input();
  ping_and_reconnect();
  disabled_keepalive_timers();
  repeated_reconnect_failures_keep_retrying();
  backoff_doubles_up_to_the_longest_wait();
  backoff_starts_over_after_a_reconnect();
  jitter_cuts_each_wait_by_up_to_half();
  reconnecting_stops_at_the_servers_grace();
  attempt_failing_after_the_grace_gives_up_at_once();
  reconnecting_stops_when_the_token_is_refused();
  reconnecting_stops_when_the_room_is_closed();
  failures_say_how_far_the_connection_got();
  initial_connection_failures();
  server_errors();
  resume_with_fallback_to_join();
  leaving();
  forward_and_commands_by_state();
  invalid_arguments();
  bad_server_input_is_ignored();
  room_updates_are_delivered();
  oversized_values_are_rejected();
  return crpp_test_result();
}
