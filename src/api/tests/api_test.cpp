// Golden values come from server/src/main.rs (routes, AuthResponse, MeResponse,
// Availability, locked_response and login's invalid_credentials body).
#include <string>

#include "check.hpp"
#include "crpp/api/api.hpp"

using namespace crpp::api;

namespace {

HttpResult reply(int status, std::string body) { return Response{status, std::move(body)}; }

void test_requests_match_the_routes() {
  CHECK(check_username("a.b_c").method == Method::Get);
  CHECK(check_username("a.b_c").path == "api/username?name=a.b_c");
  CHECK(check_username("a b&c").path == "api/username?name=a%20b%26c");
  CHECK(check_username("a").token.empty());

  const Request reg = register_account("alice", "pass\"word1");
  CHECK(reg.method == Method::Post && reg.path == "api/register" && reg.token.empty());
  const std::string reg_body = R"({"username":"alice","password":"pass\"word1"})";  // MSVC cannot stringize it in CHECK
  CHECK(reg.body == reg_body);
  CHECK(login("alice", "x").path == "api/login");

  CHECK(get_me("t").method == Method::Get && get_me("t").path == "api/me" && get_me("t").token == "t");
  CHECK(logout("t").method == Method::Post && logout("t").path == "api/logout" && logout("t").token == "t");

  const Request nick = set_nickname("t", "구름");
  CHECK(nick.method == Method::Put && nick.path == "api/nickname" && nick.token == "t");
  CHECK(nick.body == R"({"nickname":"구름"})");

  CHECK(check_handle("t", "cloud").path == "api/handle?name=cloud" && check_handle("t", "cloud").token == "t");
  const Request handle = set_handle("t", "cloud");
  CHECK(handle.method == Method::Put && handle.path == "api/handle" && handle.body == R"({"handle":"cloud"})");
}

void test_reads_answers() {
  const auto free = read_availability(reply(200, R"({"valid":true,"available":false})"));
  CHECK(free && free->valid && !free->available);

  const auto auth = read_auth(reply(200, R"({"token":"T","username":"alice","handle":"cloud"})"));
  CHECK(auth && auth->token == "T" && auth->me.username == "alice" && auth->me.handle == "cloud" && auth->me.nickname.empty());

  const auto me = read_me(reply(200, R"({"username":"alice","nickname":"구름"})"));
  CHECK(me && me->nickname == "구름" && me->handle.empty());

  CHECK(read_status(reply(204, "")).has_value());
}

void test_reads_failures() {
  const auto locked = read_auth(reply(429, R"({"error":"locked","retryAfter":180})"));
  CHECK(!locked && locked.error().code == "locked" && locked.error().retry_after == 180 && locked.error().status == 429);

  const auto wrong = read_auth(reply(401, R"({"error":"invalid_credentials","attempts":2,"max":5})"));
  CHECK(!wrong && wrong.error().code == "invalid_credentials" && wrong.error().attempts == 2 && wrong.error().max_attempts == 5);

  const auto taken = read_status(reply(409, R"({"error":"handle_taken"})"));
  CHECK(!taken && taken.error().code == "handle_taken");

  // Not every failing response comes from the server's own handlers.
  const auto proxy = read_me(reply(502, "<html>bad gateway</html>"));
  CHECK(!proxy && proxy.error().code == "http_502" && proxy.error().status == 502);
  const auto missing = read_status(reply(404, ""));
  CHECK(!missing && missing.error().status == 404);

  const auto network = read_me(std::unexpected(HttpError::Network));
  CHECK(!network && network.error().code == "network" && network.error().status == 0);
  CHECK(!read_status(std::unexpected(HttpError::Network)));
}

void test_rejects_what_is_not_the_promised_shape() {
  CHECK(!read_auth(reply(200, "not json")));
  CHECK(!read_auth(reply(200, R"({"username":"alice"})")));  // no token
  CHECK(!read_availability(reply(200, R"({"valid":true})")));
  CHECK(read_me(reply(200, "[]")).error().code == "bad_response");
}

void test_server_address() {
  const auto plain = parse_server_address("https://cr.example.com/");
  CHECK(plain && plain->secure && plain->host == "cr.example.com" && plain->port == 0 && plain->prefix == "/");

  const auto no_scheme = parse_server_address("cr.example.com");
  CHECK(no_scheme && no_scheme->secure && no_scheme->prefix == "/");

  const auto local = parse_server_address("http://127.0.0.1:8080/base?x=1");
  CHECK(local && !local->secure && local->host == "127.0.0.1" && local->port == 8080 && local->prefix == "/base/");

  const auto ipv6 = parse_server_address("http://[::1]:9000/");
  CHECK(ipv6 && ipv6->host == "[::1]" && ipv6->port == 9000);
  const auto ipv6_bare = parse_server_address("http://[::1]/");
  CHECK(ipv6_bare && ipv6_bare->host == "[::1]" && ipv6_bare->port == 0);

  CHECK(!parse_server_address(""));
  CHECK(!parse_server_address("ftp://x/"));
  CHECK(!parse_server_address("https://user@x/"));
  CHECK(!parse_server_address("https://x:notaport/"));
  CHECK(!parse_server_address("https://x:99999/"));
}

}  // namespace

int main() {
  test_requests_match_the_routes();
  test_reads_answers();
  test_reads_failures();
  test_rejects_what_is_not_the_promised_shape();
  test_server_address();
  return crpp_test_result();
}
