#pragma once
// Runs a Session against a Transport on its own thread.
//
// Client owns the Session, a mailbox and one driver thread. Commands may be
// issued from any thread and never block; everything that happens (state
// changes, room messages, failures) is reported to the Observer on the driver
// thread. The Observer must not call stop() or destroy the Client from inside
// a callback.
//
// The Client applies the Session driver contract: it feeds Ticks at
// deadline(), performs I/O actions in order, drops events of a connection
// after it was closed, and disables the Session timers the transport cannot
// serve (Transport::capabilities()).
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "crpp/session/session.hpp"
#include "crpp/transport/transport.hpp"
#include "crpp/wire/client_message.hpp"
#include "crpp/wire/input_frame.hpp"
#include "crpp/wire/server_message.hpp"

namespace crpp::client {

struct Config {
  std::string server_url;  // as the user typed it; https is assumed without a scheme
  std::string nickname;
  std::string token;  // empty for a guest
  session::Timing timing;
};

// Callbacks run on the driver thread; views are valid only during the call.
class Observer {
 public:
  virtual void on_status(session::State state) = 0;
  virtual void on_deliver(const wire::ServerMessage& message) = 0;
  virtual void on_peer_input(std::span<const wire::ServerEvent> events) = 0;
  virtual void on_failed(session::Failure reason, std::string_view detail) = 0;
  virtual void on_persist(const session::Persistent& state) = 0;

 protected:
  ~Observer() = default;
};

class Client {
 public:
  Client(transport::Transport& transport, Config config, Observer& observer);
  ~Client();
  Client(const Client&) = delete;
  Client& operator=(const Client&) = delete;

  void start();
  // Stops the driver thread and closes the transport. Idempotent.
  void stop();

  // --- commands (see session::event for their meaning) ---
  void create(bool is_public, bool chat);
  void join(std::string_view code);
  void join_random();
  void resume(std::string_view code, std::string_view key, std::optional<std::string_view> ticket,
              bool fallback_to_join);
  void leave();
  void set_sounds(const session::event::SetSounds& sounds);
  void input(wire::Kind kind);
  void send_chat(std::string_view text);
  void request_profile(std::uint32_t slot);
  void set_nick(std::string_view nick);
  void set_visibility(bool is_public);
  void set_chat_enabled(bool enabled);
  // Restore the guest id saved by an earlier run.
  void restore_gid(std::string_view gid);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crpp::client
