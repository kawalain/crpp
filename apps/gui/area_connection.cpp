#include <memory>
#include <mutex>
#include <thread>

#include "areas.hpp"
#include "crpp/transport/target.hpp"
#include "crpp/transport/winhttp_health.hpp"
#include "crpp/ui/connection_feedback.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui {

namespace {

// One short-lived thread per question: a blocked WinHTTP call must not hold the UI.
class WorkerProbe final : public HealthProbe {
 public:
  explicit WorkerProbe(Shell& s) : s_(s) {}
  ~WorkerProbe() override {
    {
      std::lock_guard lock(mutex_);
      closing_ = true;
      if (health_) health_->cancel();
    }
    if (thread_.joinable()) thread_.join();
  }

  void start(std::function<void(bool)> done) override {
    if (thread_.joinable()) thread_.join();  // it posted its answer and is ending
    auto health = std::make_shared<transport::WinHttpHealth>();
    {
      std::lock_guard lock(mutex_);
      if (closing_) return;
      health_ = health;
    }
    thread_ = std::thread([this, health, url = s_.config.server_url, done = std::move(done)]() mutable {
      const auto target = transport::make_health_target(url);
      const bool reachable = target && health->reachable(*target);
      s_.tasks.post([done = std::move(done), reachable] { done(reachable); });
    });
  }

 private:
  Shell& s_;
  std::mutex mutex_;
  bool closing_ = false;
  std::shared_ptr<transport::WinHttpHealth> health_;
  std::thread thread_;
};

}  // namespace

struct ConnectionArea {
  explicit ConnectionArea(Shell& s) : probe(s), feedback(s.app, probe) {}
  WorkerProbe probe;
  ConnectionFeedback feedback;
};

}  // namespace crpp::gui

namespace crpp::gui::area::connection {

void wire(Shell& s) {
  Shell* sp = &s;
  s.connection = std::make_shared<ConnectionArea>(s);
  s.bridge->on_room_changed = [sp](bool in_room) {
    if (in_room) (void)sp->input->start();
    else sp->input->stop();
  };
  s.bridge->on_unauthorized = [sp] { area::account::token_rejected(*sp); };
  s.bridge->on_state = [sp](session::State state) { sp->connection->feedback.state_changed(state); };
  s.bridge->on_failure = [sp](session::Failure reason, const std::string& detail) {
    sp->connection->feedback.failed(reason, detail, sp->bridge->in_room());
  };
}

void check_server(Shell& s, std::function<void()> then) {
  // A throwaway data folder with no server of its own stays off the default one, so a
  // scripted run touches no network unless it asks to.
  if (s.throwaway_data && s.store->string_or("serverUrl", "").empty()) {
    then();
    return;
  }
  s.connection->feedback.check_server(std::move(then));
}

void server_unreachable(Shell& s) { s.connection->feedback.server_unreachable(); }

namespace {

void demo_room(Shell& s) {
  s.app.show_session("ABC234");
  s.app.room().set_room({{0, "크롬이", "", false}, {1, "구름이", "", false}, {2, "비구름", "", true}}, 0, 0, false, false);
}

}  // namespace

bool scene(Shell& s, std::string_view name) {
  if (name == "setup" || name == "first-run") {
    s.app.show_setup({.files_done = 76, .files = 180, .bytes_done = 7'046'430, .bytes = 16'777'216});
    return true;
  }
  if (name == "setup-failed" || name == "first-run-failed") {
    s.app.show_setup({.files_done = 76, .files = 180, .bytes_done = 7'046'430, .bytes = 16'777'216, .failed = true});
    return true;
  }
  if (name == "offline" || name == "offline-dark") {
    if (name == "offline-dark") s.app.set_theme("darkcloud");
    s.app.offline().show();
    return true;
  }
  if (name == "reconnecting" || name == "reconnecting-dark") {
    if (name == "reconnecting-dark") s.app.set_theme("darkcloud");
    demo_room(s);
    s.app.offline().show();
    return true;
  }
  return false;
}

}  // namespace crpp::gui::area::connection
