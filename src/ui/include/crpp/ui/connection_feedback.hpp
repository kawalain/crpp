#pragma once
// What the user is told about the connection: why a Room could not be entered or was
// lost, and that the server cannot be reached. UI thread only.
//
// Reachability is asked when there is a reason to: at startup, when a connection
// failed without reaching the server, and every 5 s only while the popup says the
// server is gone. Nothing runs while all is well.
#include <functional>
#include <string>
#include <string_view>

#include "crpp/session/session.hpp"
#include "crpp/ui/app.hpp"

namespace crpp::ui {

// The seam for GET /health.
class HealthProbe {
 public:
  virtual ~HealthProbe() = default;
  // Asks once. `done` runs on the UI thread, possibly before start returns. Never
  // called again until `done` has run.
  virtual void start(std::function<void(bool reachable)> done) = 0;
};

// Upstream's wording. in_room: the Room had been entered, so this is a reconnect ending.
[[nodiscard]] std::string_view failure_text(session::Failure reason, std::string_view detail, bool in_room);

class ConnectionFeedback {
 public:
  static constexpr unsigned long long retry_ms = 5000;

  ConnectionFeedback(App& app, HealthProbe& probe);

  // Startup: runs `then` once the server answers, however long that takes. If a Room
  // is being entered by then, `then` is dropped: the user already moved on.
  void check_server(std::function<void()> then);
  // A request of another kind failed on the network.
  void server_unreachable();

  void state_changed(session::State state);
  void failed(session::Failure reason, std::string_view detail, bool in_room);

 private:
  void ask();
  void answered(bool reachable);
  void tell(std::string_view text, bool in_room);

  App& app_;
  HealthProbe& probe_;
  session::State state_ = session::State::Idle;
  bool asking_ = false;
  bool offline_ = false;       // the popup is up because the server is gone
  bool reconnecting_ = false;  // the popup is up because a Room's connection is lost
  std::string pending_;        // said if the server turns out to be there
  std::function<void()> then_;
};

}  // namespace crpp::ui
