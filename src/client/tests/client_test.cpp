// End to end: Client + Session + WinHttpTransport against a scripted room
// server on localhost.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include "check.hpp"
#include "crpp/client/client.hpp"
#include "crpp/transport/winhttp_transport.hpp"
#include "mock_ws_server.hpp"

using namespace crpp;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

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

const char* name(session::Failure f) {
  switch (f) {
    case session::Failure::Unauthorized: return "Unauthorized";
    case session::Failure::ConnectFailed: return "ConnectFailed";
    case session::Failure::RoomGone: return "RoomGone";
    case session::Failure::ServerError: return "ServerError";
    case session::Failure::InvalidArgument: return "InvalidArgument";
    case session::Failure::RoomClosed: return "RoomClosed";
    case session::Failure::GraceExpired: return "GraceExpired";
  }
  return "?";
}

class Recorder final : public client::Observer {
 public:
  void on_status(session::State s) override { add(std::string("status:") + name(s)); }
  void on_deliver(const wire::ServerMessage& m) override {
    if (std::holds_alternative<wire::msg::Joined>(m)) add("joined");
    else if (const auto* c = std::get_if<wire::msg::Chat>(&m)) add("chat:" + std::string(c->text));
    else if (std::holds_alternative<wire::msg::Members>(m)) add("members");
  }
  void on_peer_input(std::span<const wire::ServerEvent> e) override {
    for (const auto& x : e) add("peer:" + std::to_string(x.slot) + "," + std::to_string(static_cast<int>(x.kind)));
  }
  void on_failed(session::Failure f, std::string_view detail) override {
    add(std::string("failed:") + name(f) + ":" + std::string(detail));
  }
  void on_persist(const session::Persistent& p) override {
    add("persist:" + std::string(p.code) + "/" + std::string(p.key) + "/" + std::string(p.gid));
  }

  bool wait_for(const std::string& event, std::chrono::milliseconds limit = 8s) {
    std::unique_lock lock(m_);
    return cv_.wait_for(lock, limit, [&] { return has(event); });
  }
  std::size_t count(const std::string& event) {
    std::lock_guard lock(m_);
    std::size_t n = 0;
    for (const auto& e : events_) n += e == event ? 1 : 0;
    return n;
  }
  std::vector<std::string> statuses() {
    std::lock_guard lock(m_);
    std::vector<std::string> r;
    for (const auto& e : events_) if (e.rfind("status:", 0) == 0) r.push_back(e.substr(7));
    return r;
  }

 private:
  void add(std::string e) {
    { std::lock_guard lock(m_); events_.push_back(std::move(e)); }
    cv_.notify_all();
  }
  bool has(const std::string& event) const {
    for (const auto& e : events_) if (e == event) return true;
    return false;
  }
  std::mutex m_;
  std::condition_variable cv_;
  std::vector<std::string> events_;
};

std::string extract(const std::string& json, const std::string& key) {
  const std::string needle = "\"" + key + "\":\"";
  const auto p = json.find(needle);
  if (p == std::string::npos) return {};
  const auto start = p + needle.size();
  return json.substr(start, json.find('"', start) - start);
}

// What the scripted server saw, shared with the test.
struct Seen {
  std::mutex m;
  std::vector<std::string> control;  // join/create/resume messages, in order
  std::vector<std::string> inputs;   // binary frames as "16,"
};

// Plays the part of a room server: confirms create/join/resume, echoes chat,
// answers leave. The first connection can be made to drop after joining.
mock::Server::Script room_script(Seen& seen, std::atomic<int>& connection_no, bool drop_first) {
  return [&seen, &connection_no, drop_first](mock::Conn& c) {
    const int n = ++connection_no;
    if (c.header("Authorization") == "Bearer bad") {
      c.reply(401);
      return;
    }
    c.accept_ws();
    bool chat_on = false;
    for (;;) {
      unsigned char op;
      std::string p;
      if (!c.read_frame(op, p)) return;
      if (op == 2) {
        std::string s;
        for (const char ch : p) s += std::to_string(static_cast<unsigned char>(ch)) + ",";
        std::lock_guard lock(seen.m);
        seen.inputs.push_back(s);
        continue;
      }
      if (op != 1) continue;
      const std::string t = extract(p, "t");
      if (t == "create" || t == "join" || t == "resume") {
        { std::lock_guard lock(seen.m); seen.control.push_back(p); }
        const std::string code = t == "create" ? "ABC234" : extract(p, "code");
        chat_on = t != "create" || p.find("\"chat\":true") != std::string::npos;
        if (extract(p, "gid").empty()) c.send_frame(1, R"({"t":"gid","gid":"G"})");
        c.send_frame(1, R"({"t":"joined","code":")" + code +
                            R"(","me":0,"members":[{"slot":0,"order":1,"offline":false,"nick":"bob","cid":"c0"}],"host":0,"public":false,"chat":true})");
        c.send_frame(1, R"({"t":"resume","code":")" + code + R"(","key":"K)" + std::to_string(n) + R"("})");
        c.send_frame(1, R"({"t":"ticket","code":")" + code + R"(","ticket":"T)" + std::to_string(n) + R"("})");
        if (t == "create") c.send_frame(2, std::string("\x01\x10", 2));  // a peer types
        if (drop_first && n == 1) {
          std::this_thread::sleep_for(150ms);
          return;  // the connection just disappears
        }
      } else if (t == "chat" && chat_on) {
        c.send_frame(1, R"({"t":"chat","slot":0,"cid":"c0","nick":"bob","text":")" + extract(p, "text") +
                            R"(","time":1})");
      } else if (t == "leave") {
        c.send_frame(1, R"({"t":"left"})");
      }
    }
  };
}

client::Config config_for(const mock::Server& server, std::string token = {}) {
  client::Config c;
  c.server_url = "http://127.0.0.1:" + std::to_string(server.port());
  c.nickname = "bob";
  c.token = std::move(token);
  c.timing.reconnect_first = 200ms;
  return c;
}

void join_chat_and_leave() {
  Seen seen;
  std::atomic<int> n{0};
  mock::Server server(room_script(seen, n, false));
  Recorder rec;
  transport::WinHttpTransport transport;
  client::Client client(transport, config_for(server), rec);
  client.start();

  client.set_sounds({std::string_view("01_Plastic"), std::nullopt, std::nullopt, std::nullopt});
  client.create(false, true);
  CHECK(rec.wait_for("status:InRoom"));
  CHECK(rec.wait_for("joined"));
  CHECK(rec.wait_for("peer:1,16"));
  CHECK(rec.wait_for("persist:ABC234/K1/G"));

  client.send_chat("hello");
  CHECK(rec.wait_for("chat:hello"));

  client.input(wire::Kind::KeyNormal);
  const auto deadline = Clock::now() + 5s;
  for (;;) {
    { std::lock_guard lock(seen.m); if (!seen.inputs.empty() || Clock::now() > deadline) break; }
    std::this_thread::sleep_for(10ms);
  }
  {
    std::lock_guard lock(seen.m);
    CHECK(seen.inputs.size() == 1);
    CHECK(seen.inputs.size() == 1 && seen.inputs[0] == "16,");
    CHECK(seen.control.size() == 1);
  }

  client.leave();
  CHECK(rec.wait_for("status:Idle"));
  const std::vector<std::string> expected{"Connecting", "Joining", "InRoom", "Leaving", "Idle"};
  CHECK(rec.statuses() == expected);
  client.stop();
}

void reconnects_with_the_held_seat() {
  Seen seen;
  std::atomic<int> n{0};
  mock::Server server(room_script(seen, n, true));
  Recorder rec;
  transport::WinHttpTransport transport;
  client::Client client(transport, config_for(server), rec);
  client.start();
  client.create(false, true);

  CHECK(rec.wait_for("status:Waiting"));
  // Back in the room after the second connection.
  const auto deadline = Clock::now() + 8s;
  while (rec.count("status:InRoom") < 2 && Clock::now() < deadline) std::this_thread::sleep_for(10ms);
  CHECK(rec.count("status:InRoom") == 2);
  CHECK(rec.wait_for("persist:ABC234/K2/G"));  // the new key replaced the old

  {
    std::lock_guard lock(seen.m);
    CHECK(seen.control.size() == 2);
    if (seen.control.size() == 2) {
      CHECK(extract(seen.control[1], "t") == "resume");
      CHECK(extract(seen.control[1], "key") == "K1");
      CHECK(extract(seen.control[1], "ticket") == "T1");
      CHECK(extract(seen.control[1], "gid") == "G");
    }
  }
  client.send_chat("after");
  CHECK(rec.wait_for("chat:after"));
  client.stop();
}

void unauthorized_token() {
  Seen seen;
  std::atomic<int> n{0};
  mock::Server server(room_script(seen, n, false));
  Recorder rec;
  transport::WinHttpTransport transport;
  client::Client client(transport, config_for(server, "bad"), rec);
  client.start();
  client.create(false, false);
  CHECK(rec.wait_for("failed:Unauthorized:"));
  CHECK(rec.wait_for("status:Idle"));
  client.stop();
}

void unusable_server_url() {
  Recorder rec;
  transport::WinHttpTransport transport;
  client::Config c;
  c.server_url = "ftp://example.invalid";
  c.nickname = "bob";
  client::Client client(transport, c, rec);
  client.start();
  client.join_random();
  CHECK(rec.wait_for("failed:ConnectFailed:"));
  client.stop();
}

void stop_is_prompt_even_while_connecting() {
  mock::Server server([](mock::Conn& c) {
    char ch;
    while (::recv(c.sock, &ch, 1, 0) > 0) {}  // never answers
  });
  Recorder rec;
  transport::WinHttpTransport transport;
  client::Client client(transport, config_for(server), rec);
  client.start();
  client.create(false, false);
  CHECK(rec.wait_for("status:Connecting"));
  std::this_thread::sleep_for(200ms);
  const auto start = Clock::now();
  client.stop();
  CHECK(Clock::now() - start < 3s);
}

}  // namespace

int main() {
  join_chat_and_leave();
  reconnects_with_the_held_seat();
  unauthorized_token();
  unusable_server_url();
  stop_is_prompt_even_while_connecting();
  return crpp_test_result();
}
