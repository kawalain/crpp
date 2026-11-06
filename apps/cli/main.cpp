// crpp_cli: join (or create) a Cloudroom room, send one chat message and wait
// until the server delivers it back. A smoke test for the whole client stack.
//
//   crpp_cli <server-url> <nickname> create <message>
//   crpp_cli <server-url> <nickname> join <code> <message>
//
// Exit code 0 only if the message came back from the room.
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

#include "crpp/client/client.hpp"
#include "crpp/transport/winhttp_transport.hpp"

using namespace crpp;
using namespace std::chrono_literals;

namespace {

const char* name(session::State s) {
  switch (s) {
    case session::State::Idle: return "Idle";
    case session::State::Connecting: return "Connecting";
    case session::State::Joining: return "Joining";
    case session::State::InRoom: return "InRoom";
    case session::State::Leaving: return "Leaving";
    case session::State::Waiting: return "Waiting";
  }
  return "?";
}

class Printer final : public client::Observer {
 public:
  explicit Printer(std::string message) : message_(std::move(message)) {}

  void on_status(session::State s) override {
    std::printf("state: %s\n", name(s));
    std::lock_guard lock(m_);
    state_ = s;
    cv_.notify_all();
  }
  void on_deliver(const wire::ServerMessage& m) override {
    if (const auto* j = std::get_if<wire::msg::Joined>(&m)) {
      std::printf("joined room %.*s as slot %u (%zu members)\n", static_cast<int>(j->code.size()),
                  j->code.data(), j->me, j->room.members.size());
    } else if (const auto* c = std::get_if<wire::msg::Chat>(&m)) {
      std::printf("chat from %.*s: %.*s\n", static_cast<int>(c->nick.size()), c->nick.data(),
                  static_cast<int>(c->text.size()), c->text.data());
      if (c->text == message_) {
        std::lock_guard lock(m_);
        delivered_ = true;
        cv_.notify_all();
      }
    }
  }
  void on_peer_input(std::span<const wire::ServerEvent>) override {}
  void on_failed(session::Failure reason, std::string_view detail) override {
    std::printf("failed: reason=%d detail=%.*s\n", static_cast<int>(reason),
                static_cast<int>(detail.size()), detail.data());
    std::lock_guard lock(m_);
    failed_ = true;
    cv_.notify_all();
  }
  void on_persist(const session::Persistent&) override {}

  template <class Pred>
  bool wait(Pred pred, std::chrono::seconds limit) {
    std::unique_lock lock(m_);
    return cv_.wait_for(lock, limit, [&] { return failed_ || pred(); }) && !failed_;
  }
  bool in_room() { return state_ == session::State::InRoom; }
  bool delivered() { return delivered_; }

 private:
  std::string message_;
  std::mutex m_;
  std::condition_variable cv_;
  session::State state_ = session::State::Idle;
  bool delivered_ = false;
  bool failed_ = false;
};

int usage() {
  std::fprintf(stderr,
               "usage: crpp_cli <server-url> <nickname> create <message>\n"
               "       crpp_cli <server-url> <nickname> join <code> <message>\n");
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 5) return usage();
  const std::string mode = argv[3];
  std::string code, message;
  if (mode == "create" && argc == 5) {
    message = argv[4];
  } else if (mode == "join" && argc == 6) {
    code = argv[4];
    message = argv[5];
  } else {
    return usage();
  }

  Printer printer(message);
  transport::WinHttpTransport transport;
  client::Config config;
  config.server_url = argv[1];
  config.nickname = argv[2];
  client::Client client(transport, config, printer);
  client.start();
  if (mode == "create") client.create(false, true);
  else client.join(code);

  int result = 1;
  if (printer.wait([&] { return printer.in_room(); }, 20s)) {
    client.send_chat(message);
    if (printer.wait([&] { return printer.delivered(); }, 20s)) {
      std::printf("OK: message delivered by the room\n");
      result = 0;
    } else {
      std::printf("FAILED: message was not delivered back (is chat enabled in this room?)\n");
    }
  } else {
    std::printf("FAILED: could not enter the room\n");
  }
  client.leave();
  printer.wait([&] { return !printer.in_room(); }, 5s);
  client.stop();
  return result;
}
