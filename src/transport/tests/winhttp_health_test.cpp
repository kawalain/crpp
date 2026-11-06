// WinHttpHealth against a tiny local HTTP listener.
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "check.hpp"
#include "crpp/transport/winhttp_health.hpp"

using namespace crpp::transport;
using namespace std::chrono_literals;

namespace {

// Answers one request with `reply`, or holds the connection silent when it is empty.
class Listener {
 public:
  explicit Listener(std::string reply) : reply_(std::move(reply)) {
    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    ::listen(socket_, 4);
    int size = sizeof addr;
    ::getsockname(socket_, reinterpret_cast<sockaddr*>(&addr), &size);
    port_ = ntohs(addr.sin_port);
    thread_ = std::thread([this] {
      for (;;) {
        const SOCKET c = ::accept(socket_, nullptr, nullptr);
        if (c == INVALID_SOCKET) return;
        char buf[2048];
        const int n = ::recv(c, buf, sizeof buf, 0);
        if (n > 0 && request_.empty()) request_.assign(buf, static_cast<std::size_t>(n));
        if (!reply_.empty()) ::send(c, reply_.data(), static_cast<int>(reply_.size()), 0);
        else while (!done_) std::this_thread::sleep_for(10ms);
        ::closesocket(c);
      }
    });
  }
  ~Listener() {
    done_ = true;
    ::closesocket(socket_);
    thread_.join();
  }
  [[nodiscard]] Target target() const { return *make_health_target("http://127.0.0.1:" + std::to_string(port_)); }
  [[nodiscard]] const std::string& request() const { return request_; }

 private:
  std::string reply_;
  SOCKET socket_ = INVALID_SOCKET;
  unsigned short port_ = 0;
  std::thread thread_;
  std::string request_;
  std::atomic<bool> done_{false};
};

constexpr auto ok = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";

void a_success_answer_means_reachable() {
  Listener server(ok);
  WinHttpHealth health;
  CHECK(health.reachable(server.target()));
  // The listener has its request once it replied, and reachable returned after that.
  CHECK(server.request().starts_with("GET /health HTTP/1.1"));
}

void an_error_answer_means_unreachable() {
  Listener server("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
  WinHttpHealth health;
  CHECK(!health.reachable(server.target()));
}

void nobody_listening_means_unreachable() {
  Target target;
  {
    Listener gone(ok);
    target = gone.target();
  }
  WinHttpHealth health;
  CHECK(!health.reachable(target));
}

void silence_times_out() {
  Listener server("");
  WinHttpHealth health(300ms);
  const auto start = std::chrono::steady_clock::now();
  CHECK(!health.reachable(server.target()));
  CHECK(std::chrono::steady_clock::now() - start < 3s);
}

void cancel_stops_a_blocked_question() {
  Listener server("");
  WinHttpHealth health(30s);
  const auto start = std::chrono::steady_clock::now();
  std::thread canceller([&] {
    std::this_thread::sleep_for(200ms);
    health.cancel();
  });
  CHECK(!health.reachable(server.target()));
  CHECK(std::chrono::steady_clock::now() - start < 5s);
  canceller.join();
  CHECK(!health.reachable(server.target()));  // refused at once from now on
}

}  // namespace

int main() {
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  a_success_answer_means_reachable();
  an_error_answer_means_unreachable();
  nobody_listening_means_unreachable();
  silence_times_out();
  cancel_stops_a_blocked_question();
  WSACleanup();
  return crpp_test_result();
}
