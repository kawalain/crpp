#include <string_view>

#include "check.hpp"
#include "crpp/transport/target.hpp"

using namespace crpp::transport;
using namespace std::string_view_literals;

namespace {

void expect(std::string_view url, std::string_view nick, bool secure, std::string_view host,
            unsigned port, std::string_view path, int line) {
  const auto t = make_target(url, nick);
  if (!t) {
    ++crpp::test::failures;
    std::fprintf(stderr, "line %d: unexpected error for %.*s\n", line, static_cast<int>(url.size()),
                 url.data());
    return;
  }
  if (t->secure != secure || t->host != host || t->port != port || t->path_and_query != path) {
    ++crpp::test::failures;
    std::fprintf(stderr, "line %d: got %s %s:%u %s\n", line, t->secure ? "wss" : "ws",
                 t->host.c_str(), static_cast<unsigned>(t->port), t->path_and_query.c_str());
  }
}

void expect_error(std::string_view url, TargetError error, int line) {
  const auto t = make_target(url, "n");
  if (t || t.error() != error) {
    ++crpp::test::failures;
    std::fprintf(stderr, "line %d: expected error for %.*s\n", line, static_cast<int>(url.size()),
                 url.data());
  }
}

void valid_targets() {
  expect("https://cr.example.com/", "bob", true, "cr.example.com", 443, "/ws?nick=bob", __LINE__);
  expect("https://cr.example.com", "bob", true, "cr.example.com", 443, "/ws?nick=bob", __LINE__);
  expect("cr.example.com", "bob", true, "cr.example.com", 443, "/ws?nick=bob", __LINE__);
  expect("HTTP://127.0.0.1:8008", "bob", false, "127.0.0.1", 8008, "/ws?nick=bob", __LINE__);
  expect("http://localhost", "bob", false, "localhost", 80, "/ws?nick=bob", __LINE__);
  expect("https://h:8443/base/path///", "bob", true, "h", 8443, "/base/path/ws?nick=bob", __LINE__);
  expect("https://h/p?x=1#frag", "bob", true, "h", 443, "/p/ws?nick=bob", __LINE__);
  expect("http://[::1]:9000/", "bob", false, "::1", 9000, "/ws?nick=bob", __LINE__);
  expect("http://[::1]/", "bob", false, "::1", 80, "/ws?nick=bob", __LINE__);
  expect("https://h:/", "bob", true, "h", 443, "/ws?nick=bob", __LINE__);  // empty port = default
}

void nickname_is_percent_encoded() {
  expect("http://h", "a b&c=d/é"sv, false, "h", 80,
         "/ws?nick=a%20b%26c%3Dd%2F%C3%A9", __LINE__);
  expect("http://h", "Az09-._~", false, "h", 80, "/ws?nick=Az09-._~", __LINE__);
  expect("http://h", "", false, "h", 80, "/ws?nick=", __LINE__);
}

void health_targets() {
  const auto path = [](std::string_view url) {
    const auto t = make_health_target(url);
    return t ? t->path_and_query : std::string("(invalid)");
  };
  CHECK(path("https://cr.example.com/") == "/health");
  CHECK(path("cr.example.com") == "/health");
  CHECK(path("https://h:8443/base/path///") == "/base/path/health");
  CHECK(path("ftp://h/") == "(invalid)");
  const auto t = make_health_target("http://127.0.0.1:8008");
  CHECK(t && !t->secure && t->host == "127.0.0.1" && t->port == 8008);
}

void invalid_targets() {
  expect_error("ftp://h/", TargetError::UnsupportedScheme, __LINE__);
  expect_error("ws://h/", TargetError::UnsupportedScheme, __LINE__);
  expect_error("", TargetError::Invalid, __LINE__);
  expect_error("https://", TargetError::Invalid, __LINE__);
  expect_error("https:///path", TargetError::Invalid, __LINE__);
  expect_error("https://user@h/", TargetError::Invalid, __LINE__);
  expect_error("https://h:0/", TargetError::Invalid, __LINE__);
  expect_error("https://h:65536/", TargetError::Invalid, __LINE__);
  expect_error("https://h:12a/", TargetError::Invalid, __LINE__);
  expect_error("https://[::1/", TargetError::Invalid, __LINE__);
  expect_error("https://[]/", TargetError::Invalid, __LINE__);
  expect_error("https://[::1]x/", TargetError::Invalid, __LINE__);
  expect_error("https://a b/", TargetError::Invalid, __LINE__);
}

void typed_server_addresses() {
  CHECK(server_address("cr.example.com") == "https://cr.example.com/");
  CHECK(server_address("  HTTPS://CR.Example.com/Base ") == "https://cr.example.com/Base/");
  CHECK(server_address("https://cr.example.com:8443/") == "https://cr.example.com:8443/");
  CHECK(server_address("localhost:8080") == "http://localhost:8080/");
  CHECK(server_address("http://127.0.0.1:8080") == "http://127.0.0.1:8080/");
  CHECK(server_address("http://[::1]:8080/") == "http://[::1]:8080/");
  CHECK(server_address("https://localhost:8443") == "https://localhost:8443/");
  CHECK(server_address("http://cr.example.com/") == std::unexpected(AddressProblem::NotSecure));
  // These look like this machine, but are names anyone can register.
  CHECK(server_address("http://127.0.0.1.example.com/") == std::unexpected(AddressProblem::NotSecure));
  CHECK(server_address("http://localhost.example.com/") == std::unexpected(AddressProblem::NotSecure));
  CHECK(server_address("") == std::unexpected(AddressProblem::Invalid));
  CHECK(server_address("ftp://cr.example.com/") == std::unexpected(AddressProblem::Invalid));
  CHECK(server_address("wss://cr.example.com/") == std::unexpected(AddressProblem::Invalid));
  CHECK(server_address("https://me@cr.example.com/") == std::unexpected(AddressProblem::Invalid));
  CHECK(server_address("https://cr.example.com/?x=1") == std::unexpected(AddressProblem::Invalid));
  CHECK(server_address("https://cr.example.com/#x") == std::unexpected(AddressProblem::Invalid));
  CHECK(server_address("https://cr example.com/") == std::unexpected(AddressProblem::Invalid));
  CHECK(server_address("https://cr.\xEC\x98\x88.com/") == std::unexpected(AddressProblem::Invalid));
  CHECK(server_address("https://cr.example.com:99999/") == std::unexpected(AddressProblem::Invalid));
}

void a_cloudroom_server_answers_health() {
  CHECK(is_cloudroom_health("ok\nrooms=3\npublic_rooms=1"));
  CHECK(is_cloudroom_health("ok\r\nrooms=0\r\npublic_rooms=0\r\n"));
  CHECK(!is_cloudroom_health("ok"));
  CHECK(!is_cloudroom_health("OK\nrooms=3\npublic_rooms=1"));
  CHECK(!is_cloudroom_health("<html>ok</html>\nrooms=3\npublic_rooms=1"));
  CHECK(!is_cloudroom_health("ok\nrooms=x\npublic_rooms=1"));
  CHECK(!is_cloudroom_health(""));
}

}  // namespace

int main() {
  valid_targets();
  nickname_is_percent_encoded();
  invalid_targets();
  health_targets();
  typed_server_addresses();
  a_cloudroom_server_answers_health();
  return crpp_test_result();
}
