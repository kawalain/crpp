// Against a scripted server on localhost.
#include <atomic>
#include <mutex>
#include <string>

#include "check.hpp"
#include "crpp/api/winhttp_client.hpp"
#include "mock_ws_server.hpp"

using namespace crpp::api;
using mock::Conn;
using mock::Server;

namespace {

void answer(Conn& c, int status, const std::string& body, const std::string& extra = {}) {
  const std::string r = "HTTP/1.1 " + std::to_string(status) + " X\r\nContent-Length: " + std::to_string(body.size()) +
                        "\r\nConnection: close\r\n" + extra + "\r\n" + body;
  c.send_all(r.data(), r.size());
}

std::string read_body(Conn& c) {
  const std::string length = c.header("Content-Length");
  if (length == "<absent>") return {};
  std::string body(static_cast<std::size_t>(std::stoi(length)), '\0');
  c.recv_all(body.data(), body.size());
  return body;
}

void test_sends_what_the_request_says() {
  std::mutex m;
  std::string line, auth, type, body;
  Server server([&](Conn& c) {
    std::string received = read_body(c);
    {
      std::lock_guard lock(m);
      line = c.request_line();
      auth = c.header("Authorization");
      type = c.header("Content-Type");
      body = std::move(received);
    }
    answer(c, 200, R"({"ok":true})");
  });
  WinHttpClient client("http://127.0.0.1:" + std::to_string(server.port()) + "/base");
  const auto result = client.send(set_nickname("T0K", "구름"));
  CHECK(result && result->status == 200 && result->body == R"({"ok":true})");
  std::lock_guard lock(m);
  CHECK(line == "PUT /base/api/nickname HTTP/1.1");
  CHECK(auth == "Bearer T0K");
  CHECK(type == "application/json");
  CHECK(body == R"({"nickname":"구름"})");
}

void test_no_token_no_header() {
  std::mutex m;
  std::string line, auth;
  Server server([&](Conn& c) {
    {
      std::lock_guard lock(m);
      line = c.request_line();
      auth = c.header("Authorization");
    }
    answer(c, 200, R"({"valid":true,"available":true})");
  });
  WinHttpClient client("http://127.0.0.1:" + std::to_string(server.port()) + "/");
  const auto result = read_availability(client.send(check_username("alice")));
  CHECK(result && result->available);
  std::lock_guard lock(m);
  CHECK(line == "GET /api/username?name=alice HTTP/1.1");
  CHECK(auth == "<absent>");
}

void test_an_error_status_is_a_response() {
  Server server([](Conn& c) {
    (void)read_body(c);
    answer(c, 429, R"({"error":"locked","retryAfter":180})");
  });
  WinHttpClient client("http://127.0.0.1:" + std::to_string(server.port()) + "/");
  const auto auth = read_auth(client.send(login("alice", "x")));
  CHECK(!auth && auth.error().code == "locked" && auth.error().retry_after == 180);
}

void test_a_redirect_is_not_followed() {
  std::atomic<int> requests{0};
  Server server([&](Conn& c) {
    ++requests;
    (void)read_body(c);
    answer(c, 307, "", "Location: /elsewhere\r\n");
  });
  WinHttpClient client("http://127.0.0.1:" + std::to_string(server.port()) + "/");
  const auto result = client.send(login("alice", "secret1"));
  CHECK(result && result->status == 307);
  CHECK(requests == 1);
}

void test_no_server_is_a_network_error() {
  int port = 0;
  {
    Server gone([](Conn&) {});
    port = gone.port();
  }
  WinHttpClient client("http://127.0.0.1:" + std::to_string(port) + "/");
  const auto result = client.send(get_me("T"));
  CHECK(!result && result.error() == HttpError::Network);

  WinHttpClient nonsense("ftp://x/");
  CHECK(!nonsense.send(get_me("T")));
}

}  // namespace

int main() {
  test_sends_what_the_request_says();
  test_no_token_no_header();
  test_an_error_status_is_a_response();
  test_a_redirect_is_not_followed();
  test_no_server_is_a_network_error();
  return crpp_test_result();
}
