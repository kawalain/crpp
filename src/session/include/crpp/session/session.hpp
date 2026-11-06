#pragma once
// The connection life cycle of one Cloudroom client, as a pure state machine.
//
// Session never does I/O and never reads a clock. A driver feeds it events
// (user commands, transport results, received frames, timer ticks) together
// with the current time, and performs the actions it returns. This makes every
// timing rule (15 s connect/join timeouts, 20 s ping, reconnect backoff,
// 100 ms input throttle) testable without a network.
//
// Driver contract:
//  - Call on() from one thread at a time.
//  - Perform the returned actions in order before the next call; they (and the
//    strings inside them) are valid only until then. A view passed in an event
//    is likewise only read during that call, but views inside an action may
//    point at it, so perform the actions before releasing your buffers.
//  - After a Close action, discard every later event of that connection. The
//    next Connect action starts a new one.
//  - Send a Tick when now >= deadline().
//  - Heard means "any frame arrived from the server", including WebSocket
//    ping/pong control frames the transport handles itself.
//
// Spec: upstream client/src-tauri/src/lib.rs (connect sequence, 15 s timeouts,
// 20 s ping), client/src/main.js (retry and error handling) and
// server/src/main.rs. The liveness timeout is a crpp addition.
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

#include "crpp/session/fixed_string.hpp"
#include "crpp/wire/client_message.hpp"
#include "crpp/wire/input_frame.hpp"
#include "crpp/wire/server_message.hpp"

namespace crpp::session {

using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;

struct Timing {
  std::chrono::milliseconds connect_timeout{15000};
  std::chrono::milliseconds join_timeout{15000};
  // Zero disables a timer. Disable both when the transport cannot send pings
  // or report control frames (see transport::Capabilities).
  std::chrono::milliseconds ping_interval{20000};
  // Server pings every 25 s; silence for three periods means a dead link.
  std::chrono::milliseconds liveness_timeout{75000};
  std::chrono::milliseconds leave_timeout{5000};
  // After the connection of an established room drops: the first wait, doubled on
  // every failed attempt up to the longest, each cut by jitter.
  std::chrono::milliseconds reconnect_first{1000};
  std::chrono::milliseconds reconnect_longest{30000};
  // The server's OFFLINE_GRACE: past this the Seat is gone, so retrying is pointless.
  std::chrono::milliseconds reconnect_limit{15 * 60 * 1000};
  // The server accepts 10 input events per second per member.
  std::chrono::milliseconds input_interval{100};
};

enum class State : std::uint8_t {
  Idle,        // no room, no connection
  Connecting,  // transport is opening
  Joining,     // connected; waiting for the server to confirm the seat
  InRoom,      // seat confirmed and resume key known
  Leaving,     // leave sent; waiting for the server's reply
  Waiting,     // connection lost; waiting to reconnect
};

enum class Failure : std::uint8_t {
  Unauthorized,     // token rejected (HTTP 401); the user must sign in again
  ConnectFailed,    // could not connect or join in time
  RoomGone,         // resume found no room and no usable ticket
  ServerError,      // the server refused (detail holds its error code)
  InvalidArgument,  // a command argument did not fit its limits
  RoomClosed,       // while reconnecting, the server refused the Seat
  GraceExpired,     // reconnecting went on until the server gave up the Seat
};

// Failed::detail of a ConnectFailed that got as far as the server. An empty detail
// means the server may not be there at all.
inline constexpr std::string_view detail_closed = "closed";      // closed before the join
inline constexpr std::string_view detail_no_reply = "no_reply";  // never answered the join

// Where reconnect jitter comes from: a fraction in [0, 1). Injected to keep Session
// pure. With no source, every wait is its full length.
struct Jitter {
  double (*next)(void* context) noexcept = nullptr;
  void* context = nullptr;
};

// Capacities of the strings Session keeps. Longer values are protocol errors.
inline constexpr std::size_t max_code = 8;
inline constexpr std::size_t max_key = 128;
inline constexpr std::size_t max_token = 512;  // ticket and guest id
inline constexpr std::size_t max_sound = 64;
inline constexpr std::size_t max_incoming_text = 16 * 1024;
inline constexpr std::size_t max_members = 64;

namespace event {

// --- from the user interface ---
struct Create { bool is_public{}; bool chat{}; };
struct Join { std::string_view code; };
struct JoinRandom {};
// Return to a held seat. With fallback_to_join, a missing seat falls back to a
// plain join of the same room.
struct Resume {
  std::string_view code;
  std::string_view key;
  std::optional<std::string_view> ticket;
  bool fallback_to_join{};
};
// Read resume_info() first if the seat should be remembered.
struct Leave {};
struct SetSounds {
  std::optional<std::string_view> key;
  std::optional<std::string_view> mouse;
  std::optional<std::string_view> pen;
  std::optional<std::string_view> emoji;
};
struct Input { wire::Kind kind{}; };
// Chat, Profile, Nick, Visibility, ChatEnable and ChatDisable only, and only
// while in a room.
struct Forward { wire::ClientMessage message; };

// --- from the transport ---
struct Connected {};
struct ConnectFailed { bool unauthorized{}; };
struct Closed {};
struct Text { std::string_view json; };
struct Binary { std::span<const std::uint8_t> bytes; };
struct Heard {};
struct Tick {};

}  // namespace event

using Event = std::variant<event::Create, event::Join, event::JoinRandom, event::Resume,
                           event::Leave, event::SetSounds, event::Input, event::Forward,
                           event::Connected, event::ConnectFailed, event::Closed, event::Text,
                           event::Binary, event::Heard, event::Tick>;

namespace action {

struct Connect {};                          // open the transport (cloudroom-v2 only)
struct Send { wire::ClientMessage message; };  // encode and send as a text frame
struct SendInput { wire::Kind kind{}; };    // send as a one-event input frame
struct Ping {};                             // send a WebSocket ping
struct Close {};                            // close the transport
struct Deliver { wire::ServerMessage message; };  // joined, members, chat, chat_blocked, profile
struct PeerInput { std::span<const wire::ServerEvent> events; };
struct Status { State state{}; };
struct Failed { Failure reason{}; std::string_view detail; };
struct Persist {};                          // persistent() changed; save it

}  // namespace action

using Action = std::variant<action::Connect, action::Send, action::SendInput, action::Ping,
                            action::Close, action::Deliver, action::PeerInput, action::Status,
                            action::Failed, action::Persist>;

// What must survive a restart. Views are valid until the next on().
struct Persistent {
  std::string_view code;
  std::string_view key;
  std::string_view ticket;
  std::string_view gid;
};

class Session {
 public:
  explicit Session(Timing timing = {}, Jitter jitter = {}) noexcept : timing_(timing), jitter_(jitter) {}

  [[nodiscard]] std::span<const Action> on(const Event& event, Time now) noexcept;

  // When the next Tick is due, if any timer is running.
  [[nodiscard]] std::optional<Time> deadline() const noexcept;

  [[nodiscard]] State state() const noexcept { return state_; }
  // True while a room that was entered successfully is being re-entered.
  [[nodiscard]] bool reconnecting() const noexcept;
  [[nodiscard]] Persistent persistent() const noexcept;
  // Restore the guest id saved by an earlier run. False if it does not fit.
  bool set_gid(std::string_view gid) noexcept { return gid_.assign(gid); }

 private:
  enum class Intent : std::uint8_t { Create, Join, JoinRandom, Resume };

  struct SoundField {
    FixedString<max_sound> text;
    bool present = false;
    void set(const std::optional<std::string_view>& v) noexcept;
    [[nodiscard]] std::optional<std::string_view> get() const noexcept;
  };

  void handle(const event::Create&, Time);
  void handle(const event::Join&, Time);
  void handle(const event::JoinRandom&, Time);
  void handle(const event::Resume&, Time);
  void handle(const event::Leave&, Time);
  void handle(const event::SetSounds&, Time);
  void handle(const event::Input&, Time);
  void handle(const event::Forward&, Time);
  void handle(const event::Connected&, Time);
  void handle(const event::ConnectFailed&, Time);
  void handle(const event::Closed&, Time);
  void handle(const event::Text&, Time);
  void handle(const event::Binary&, Time);
  void handle(const event::Heard&, Time);
  void handle(const event::Tick&, Time);

  void on_server_message(const wire::ServerMessage&, Time);
  void on_server_error(std::string_view detail, Time);

  void push(Action a) noexcept;
  void set_state(State s) noexcept;
  void begin_connect(Time now) noexcept;
  void become_ready(Time now) noexcept;
  void lost(Time now, bool close_needed, std::string_view detail) noexcept;
  void schedule_retry(Time now) noexcept;
  void fail(Failure reason, std::string_view detail) noexcept;
  void finish_leave() noexcept;
  void close_transport() noexcept;
  void reset_room() noexcept;
  // now + d, or Time::max() ("never") when d is zero.
  static Time after(Time now, std::chrono::milliseconds d) noexcept {
    return d.count() == 0 ? Time::max() : now + d;
  }
  void touch(Time now) noexcept { liveness_ = after(now, timing_.liveness_timeout); }
  [[nodiscard]] wire::cmd::Sounds sounds() const noexcept;
  [[nodiscard]] wire::ClientMessage control() const noexcept;

  Timing timing_;
  Jitter jitter_;
  State state_ = State::Idle;

  Intent intent_ = Intent::Join;
  bool create_public_ = false;
  bool create_chat_ = false;
  bool fallback_to_join_ = false;
  bool transport_ = false;  // a connection is opening or open
  bool got_joined_ = false;
  bool got_resume_ = false;
  bool established_ = false;  // this room has been entered at least once

  FixedString<max_code> code_;
  FixedString<max_key> key_;
  FixedString<max_token> ticket_;
  FixedString<max_token> gid_;
  SoundField key_sound_, mouse_sound_, pen_sound_, emoji_sound_;

  Time connect_deadline_{};
  Time join_deadline_{};
  Time next_ping_{};
  Time liveness_{};
  Time leave_deadline_{};
  Time retry_at_{};
  Time give_up_at_{};
  std::chrono::milliseconds next_wait_{0};  // zero: not reconnecting
  Time last_input_{};
  bool has_last_input_ = false;

  // Per-call output and parse storage.
  static constexpr std::size_t max_actions = 8;
  std::array<Action, max_actions> out_{};
  std::size_t count_ = 0;
  std::array<char, max_incoming_text> scratch_{};
  std::array<wire::MemberInfo, max_members> members_{};
  std::array<wire::ServerEvent, wire::max_frame_events> peer_events_{};
};

}  // namespace crpp::session
