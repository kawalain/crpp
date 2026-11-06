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

}  // namespace

int main() {
  valid_targets();
  nickname_is_percent_encoded();
  invalid_targets();
  return crpp_test_result();
}
