#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include "check.hpp"
#include "crpp/transport/winhttp_transport.hpp"
#include "mock_ws_server.hpp"

using namespace crpp::transport;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace {

// Records every callback as a line of text and lets a test wait for one.
class Recorder final : public Sink {
 public:
  void connected() override { add("connected"); }
  void connect_failed(bool unauthorized) override { add(unauthorized ? "failed:401" : "failed"); }
  void closed() override { add("closed"); }
  void text(std::string_view s) override { add("text:" + std::string(s)); }
  void binary(std::span<const std::uint8_t> b) override {
    std::string s = "binary:";
    for (const auto v : b) s += std::to_string(v) + ",";
    add(s);
  }
  void heard() override { add("heard"); }

  bool wait_for(const std::string& event, std::chrono::milliseconds limit = 5s) {
    std::unique_lock lock(m_);
    return cv_.wait_for(lock, limit, [&] { return contains(event); });
  }
  std::size_t count(const std::string& event) {
    std::lock_guard lock(m_);
    std::size_t n = 0;
    for (const auto& e : events_) n += e == event ? 1 : 0;
    return n;
  }
  std::vector<std::string> without_heard() {
    std::lock_guard lock(m_);
    std::vector<std::string> r;
    for (const auto& e : events_) if (e != "heard") r.push_back(e);
    return r;
  }

 private:
  void add(std::string e) {
    {
      std::lock_guard lock(m_);
      events_.push_back(std::move(e));
    }
    cv_.notify_all();
  }
  bool contains(const std::string& event) const {
    for (const auto& e : events_) if (e == event) return true;
    return false;
  }
  std::mutex m_;
  std::condition_variable cv_;
  std::vector<std::string> events_;
};

Target target_for(const mock::Server& server) {
  return *make_target("http://127.0.0.1:" + std::to_string(server.port()), "bob");
}

bool same(const std::vector<std::string>& a, const std::vector<std::string>& b) { return a == b; }

void handshake_and_messages() {
  std::string seen_request_line, seen_auth, seen_protocol;
  std::vector<std::string> from_client;
  std::mutex m;
  mock::Server server([&](mock::Conn& c) {
    {
      std::lock_guard lock(m);
      seen_request_line = c.request_line();
      seen_auth = c.header("Authorization");
      seen_protocol = c.header("Sec-WebSocket-Protocol");
    }
    c.accept_ws();
    c.send_frame(1, "{\"t\":\"left\"}");
    c.send_frame(2, std::string("\x03\x10", 2));
    c.send_frame(9, "srv-ping");  // WinHTTP answers it and does not report it
    c.send_frame(1, "after-ping");
    for (;;) {
      unsigned char op;
      std::string p;
      if (!c.read_frame(op, p)) break;
      if (op == 1 || op == 2) {
        std::lock_guard lock(m);
        from_client.push_back((op == 1 ? "t:" : "b:") + p);
      }
      if (op == 8) { c.send_frame(8, p); break; }
    }
  });

  Recorder rec;
  WinHttpTransport t;
  t.open(target_for(server), "tok123", rec);
  CHECK(rec.wait_for("text:after-ping"));
  CHECK(same(rec.without_heard(), {"connected", "text:{\"t\":\"left\"}", "binary:3,16,", "text:after-ping"}));
  {
    std::lock_guard lock(m);
    CHECK(seen_request_line == "GET /ws?nick=bob HTTP/1.1");
    CHECK(seen_auth == "Bearer tok123");
    CHECK(seen_protocol == "cloudroom-v2");
  }
  CHECK(t.send_text("hello"));
  const std::uint8_t bin[2] = {7, 9};
  CHECK(t.send_binary(bin));
  const auto deadline = Clock::now() + 5s;
  for (;;) {
    {
      std::lock_guard lock(m);
      if (from_client.size() == 2 || Clock::now() > deadline) break;
    }
    std::this_thread::sleep_for(10ms);
  }
  {
    std::lock_guard lock(m);
    CHECK(from_client.size() == 2);
    CHECK(from_client.size() == 2 && from_client[0] == "t:hello");
    CHECK(from_client.size() == 2 && from_client[1] == std::string("b:\x07\x09", 4));
  }
  t.close();
  CHECK(rec.count("closed") == 0);  // our own close() is not reported
  CHECK(!t.send_text("late"));
}

void guest_has_no_authorization() {
  std::string auth;
  std::mutex m;
  mock::Server server([&](mock::Conn& c) {
    { std::lock_guard lock(m); auth = c.header("Authorization"); }
    c.accept_ws();
    unsigned char op; std::string p;
    while (c.read_frame(op, p)) {}
  });
  Recorder rec;
  WinHttpTransport t;
  t.open(target_for(server), "", rec);
  CHECK(rec.wait_for("connected"));
  t.close();
  std::lock_guard lock(m);
  CHECK(auth == "<absent>");
}

void refused_handshakes() {
  {
    mock::Server server([](mock::Conn& c) { c.reply(401); });
    Recorder rec;
    WinHttpTransport t;
    t.open(target_for(server), "bad", rec);
    CHECK(rec.wait_for("failed:401"));
    CHECK(rec.count("connected") == 0);
  }
  {
    mock::Server server([](mock::Conn& c) { c.reply(503); });
    Recorder rec;
    WinHttpTransport t;
    t.open(target_for(server), "", rec);
    CHECK(rec.wait_for("failed"));
  }
  {
    // 101 without the subprotocol we require.
    mock::Server server([](mock::Conn& c) { c.accept_ws(""); });
    Recorder rec;
    WinHttpTransport t;
    t.open(target_for(server), "", rec);
    CHECK(rec.wait_for("failed"));
    CHECK(rec.count("connected") == 0);
  }
  {
    // A different subprotocol is not accepted either.
    mock::Server server([](mock::Conn& c) { c.accept_ws("cloudroom-v1"); });
    Recorder rec;
    WinHttpTransport t;
    t.open(target_for(server), "", rec);
    CHECK(rec.wait_for("failed"));
  }
  {
    // Nothing listens on the port.
    Target target;
    {
      mock::Server server([](mock::Conn&) {});
      target = target_for(server);
    }
    Recorder rec;
    WinHttpTransport t;
    t.open(target, "", rec);
    CHECK(rec.wait_for("failed"));
  }
  {
    // A redirect is not followed, so the token never goes to the other address.
    mock::Server elsewhere([](mock::Conn& c) { c.accept_ws(); });
    const Target there = target_for(elsewhere);
    mock::Server server([&there](mock::Conn& c) {
      const std::string r = "HTTP/1.1 307 X\r\nLocation: http://127.0.0.1:" + std::to_string(there.port) +
                            "/ws\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      c.send_all(r.data(), r.size());
    });
    Recorder rec;
    WinHttpTransport t;
    t.open(target_for(server), "secret", rec);
    CHECK(rec.wait_for("failed"));
    CHECK(rec.count("connected") == 0);
    CHECK(elsewhere.connections() == 0);
  }
  {
    // A token that could inject a header is refused before anything is sent.
    mock::Server server([](mock::Conn& c) { c.accept_ws(); });
    Recorder rec;
    WinHttpTransport t;
    t.open(target_for(server), "a\r\nX: y", rec);
    CHECK(rec.wait_for("failed"));
    CHECK(server.connections() == 0);
  }
}

void peer_closing() {
  {
    mock::Server server([](mock::Conn& c) {
      c.accept_ws();
      c.send_frame(1, "bye");
      c.send_frame(8, std::string("\x03\xE8", 2));
      unsigned char op; std::string p;
      while (c.read_frame(op, p)) {}
    });
    Recorder rec;
    WinHttpTransport t;
    t.open(target_for(server), "", rec);
    CHECK(rec.wait_for("closed"));
    CHECK(same(rec.without_heard(), {"connected", "text:bye", "closed"}));
    CHECK(rec.count("closed") == 1);
  }
  {
    // The connection just disappears.
    mock::Server server([](mock::Conn& c) {
      c.accept_ws();
      c.send_frame(1, "x");
      c.drop();
    });
    Recorder rec;
    WinHttpTransport t;
    t.open(target_for(server), "", rec);
    CHECK(rec.wait_for("closed"));
    CHECK(rec.count("closed") == 1);
  }
}

void close_wakes_a_blocked_receive_and_allows_reopen() {
  mock::Server server([](mock::Conn& c) {
    c.accept_ws();
    unsigned char op; std::string p;
    while (c.read_frame(op, p)) {}  // never sends anything
  });
  Recorder rec;
  WinHttpTransport t;
  for (int round = 0; round < 3; ++round) {
    t.open(target_for(server), "", rec);
    CHECK(rec.wait_for("connected", 5s) || round > 0);
    std::this_thread::sleep_for(150ms);  // the receive is now blocked
    const auto start = Clock::now();
    t.close();
    CHECK(Clock::now() - start < 2s);
  }
  CHECK(rec.count("connected") == 3);
  CHECK(rec.count("closed") == 0);
}

void close_during_handshake() {
  mock::Server server([](mock::Conn& c) {
    // Accepts but never answers the handshake.
    char ch;
    while (::recv(c.sock, &ch, 1, 0) > 0) {}
  });
  Recorder rec;
  WinHttpTransport t;
  t.open(target_for(server), "", rec);
  std::this_thread::sleep_for(200ms);
  const auto start = Clock::now();
  t.close();
  CHECK(Clock::now() - start < 2s);
  CHECK(rec.without_heard().empty());
}

void handshake_timeout() {
  mock::Server server([](mock::Conn& c) {
    char ch;
    while (::recv(c.sock, &ch, 1, 0) > 0) {}
  });
  Recorder rec;
  WinHttpTransport t(WinHttpOptions{.handshake_timeout = 500ms});
  const auto start = Clock::now();
  t.open(target_for(server), "", rec);
  CHECK(rec.wait_for("failed", 5s));
  CHECK(Clock::now() - start < 4s);
}

void idle_connection_outlives_the_handshake_timeout() {
  mock::Server server([](mock::Conn& c) {
    c.accept_ws();
    std::this_thread::sleep_for(2500ms);
    c.send_frame(1, "late");
    unsigned char op; std::string p;
    while (c.read_frame(op, p)) {}
  });
  Recorder rec;
  WinHttpTransport t(WinHttpOptions{.handshake_timeout = 500ms});
  t.open(target_for(server), "", rec);
  CHECK(rec.wait_for("text:late", 6s));
  CHECK(rec.count("closed") == 0);
}

void oversized_messages_are_dropped() {
  mock::Server server([](mock::Conn& c) {
    c.accept_ws();
    c.send_frame(1, std::string(60000, 'x'));  // larger than the receive buffer
    c.send_frame(1, "ok");
    unsigned char op; std::string p;
    while (c.read_frame(op, p)) {}
  });
  Recorder rec;
  WinHttpTransport t;
  t.open(target_for(server), "", rec);
  CHECK(rec.wait_for("text:ok"));
  CHECK(same(rec.without_heard(), {"connected", "text:ok"}));
}

void concurrent_senders_are_serialized() {
  std::atomic<int> received{0};
  mock::Server server([&](mock::Conn& c) {
    c.accept_ws();
    unsigned char op; std::string p;
    while (c.read_frame(op, p)) {
      if (op == 1 && p == "m") ++received;
    }
  });
  Recorder rec;
  WinHttpTransport t;
  t.open(target_for(server), "", rec);
  CHECK(rec.wait_for("connected"));
  std::vector<std::thread> threads;
  std::atomic<int> failures{0};
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&] {
      for (int k = 0; k < 50; ++k) if (!t.send_text("m")) ++failures;
    });
  }
  for (auto& th : threads) th.join();
  const auto deadline = Clock::now() + 5s;
  while (received < 200 && Clock::now() < deadline) std::this_thread::sleep_for(10ms);
  CHECK(failures == 0);
  CHECK(received == 200);
}

void capabilities_are_honest() {
  WinHttpTransport t;
  CHECK(!t.capabilities().can_ping);
  CHECK(!t.capabilities().reports_control_frames);
}

}  // namespace

int main() {
  handshake_and_messages();
  guest_has_no_authorization();
  refused_handshakes();
  peer_closing();
  close_wakes_a_blocked_receive_and_allows_reopen();
  close_during_handshake();
  handshake_timeout();
  idle_connection_outlives_the_handshake_timeout();
  oversized_messages_are_dropped();
  concurrent_senders_are_serialized();
  capabilities_are_honest();
  return crpp_test_result();
}
