#pragma once
// The account HTTP API: builds requests and reads responses, as server/src/main.rs
// defines them. Pure; the bytes go through HttpClient.
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace crpp::api {

enum class Method : std::uint8_t { Get, Post, Put };

struct Request {
  Method method = Method::Get;
  std::string path;   // relative to the server address: "api/me"
  std::string token;  // empty = no Authorization header
  std::string body;   // JSON; empty = none
};

struct Response {
  int status = 0;
  std::string body;
};

enum class HttpError : std::uint8_t { Network };
using HttpResult = std::expected<Response, HttpError>;

// Blocks until the server answers or a timeout passes, so it is only called off the
// UI thread. Calls are never concurrent. A redirect is returned, not followed: a
// password or token never goes anywhere but the server the user chose.
class HttpClient {
 public:
  [[nodiscard]] virtual HttpResult send(const Request& request) = 0;

 protected:
  ~HttpClient() = default;
};

// A request that did not succeed.
struct Failure {
  std::string code;     // the server's `error`; "network" or "bad_response" from here
  int status = 0;       // 0 when no response arrived
  int retry_after = 0;  // seconds, with code "locked"
  int attempts = 0, max_attempts = 0;  // with code "invalid_credentials" from login
};

template <class T>
using Result = std::expected<T, Failure>;

struct Availability {
  bool valid = false, available = false;
};

// What the server knows of an account. A missing nickname or handle is empty.
struct Me {
  std::string username, nickname, handle;
};

struct Auth {
  std::string token;
  Me me;
};

// --- requests ---
[[nodiscard]] Request check_username(std::string_view name);
[[nodiscard]] Request register_account(std::string_view username, std::string_view password);
[[nodiscard]] Request login(std::string_view username, std::string_view password);
[[nodiscard]] Request get_me(std::string_view token);
[[nodiscard]] Request logout(std::string_view token);
// An empty nickname clears it, which only an account with a handle may do.
[[nodiscard]] Request set_nickname(std::string_view token, std::string_view nickname);
// The token makes one's own handle count as available.
[[nodiscard]] Request check_handle(std::string_view token, std::string_view name);
[[nodiscard]] Request set_handle(std::string_view token, std::string_view handle);

// --- responses ---
[[nodiscard]] Result<Availability> read_availability(const HttpResult& result);
[[nodiscard]] Result<Auth> read_auth(const HttpResult& result);
[[nodiscard]] Result<Me> read_me(const HttpResult& result);
// For a response whose body does not matter.
[[nodiscard]] Result<void> read_status(const HttpResult& result);

// The server address the way a connection needs it. Https is assumed without a
// scheme, as the WebSocket target does. Empty if it is not an address.
struct ServerAddress {
  bool secure = true;
  std::string host;
  std::uint16_t port = 0;  // 0 = the scheme's default
  std::string prefix;      // the path before "api/...", ending in '/'
};
[[nodiscard]] std::optional<ServerAddress> parse_server_address(std::string_view url);

}  // namespace crpp::api
