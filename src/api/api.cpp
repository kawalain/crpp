#include "crpp/api/api.hpp"

#include <charconv>
#include <utility>

#include "crpp/store/json.hpp"

namespace crpp::api {

using store::Json;

namespace {

std::string encode_query(std::string_view s) {
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string out;
  for (const char c : s) {
    const auto u = static_cast<unsigned char>(c);
    if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || c == '_' || c == '.' || c == '-' ||
        c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[u >> 4];
      out += hex[u & 0xF];
    }
  }
  return out;
}

std::string credentials(std::string_view username, std::string_view password) {
  Json body = Json::object();
  body.set("username", Json(username));
  body.set("password", Json(password));
  return body.dump();
}

std::string one_field(std::string_view key, std::string_view value) {
  Json body = Json::object();
  body.set(key, Json(value));
  return body.dump();
}

int int_field(const Json& json, std::string_view key) {
  const Json* value = json.find(key);
  const auto number = value ? value->as_i64() : std::nullopt;
  return number && *number >= 0 && *number <= 1'000'000 ? static_cast<int>(*number) : 0;
}

std::string string_field(const Json& json, std::string_view key) {
  const Json* value = json.find(key);
  const auto text = value ? value->as_string() : std::nullopt;
  return text ? std::string(*text) : std::string{};
}

Failure bad_response(int status) { return {.code = "bad_response", .status = status}; }

// The parsed body of a successful response, or why there is none.
Result<Json> success_body(const HttpResult& result) {
  if (!result) return std::unexpected(Failure{.code = "network"});
  const int status = result->status;
  auto json = Json::parse(result->body);
  if (status >= 200 && status < 300) {
    if (!json || !json->is_object()) return std::unexpected(bad_response(status));
    return std::move(*json);
  }
  Failure failure{.code = "http_" + std::to_string(status), .status = status};
  if (json) {
    if (auto code = string_field(*json, "error"); !code.empty()) failure.code = std::move(code);
    failure.retry_after = int_field(*json, "retryAfter");
    failure.attempts = int_field(*json, "attempts");
    failure.max_attempts = int_field(*json, "max");
  }
  return std::unexpected(std::move(failure));
}

Me me_from(const Json& json) {
  return {string_field(json, "username"), string_field(json, "nickname"), string_field(json, "handle")};
}

}  // namespace

Request check_username(std::string_view name) {
  return {Method::Get, "api/username?name=" + encode_query(name), {}, {}};
}

Request register_account(std::string_view username, std::string_view password) {
  return {Method::Post, "api/register", {}, credentials(username, password)};
}

Request login(std::string_view username, std::string_view password) {
  return {Method::Post, "api/login", {}, credentials(username, password)};
}

Request get_me(std::string_view token) { return {Method::Get, "api/me", std::string(token), {}}; }

Request logout(std::string_view token) { return {Method::Post, "api/logout", std::string(token), {}}; }

Request set_nickname(std::string_view token, std::string_view nickname) {
  return {Method::Put, "api/nickname", std::string(token), one_field("nickname", nickname)};
}

Request check_handle(std::string_view token, std::string_view name) {
  return {Method::Get, "api/handle?name=" + encode_query(name), std::string(token), {}};
}

Request set_handle(std::string_view token, std::string_view handle) {
  return {Method::Put, "api/handle", std::string(token), one_field("handle", handle)};
}

Result<Availability> read_availability(const HttpResult& result) {
  auto json = success_body(result);
  if (!json) return std::unexpected(std::move(json.error()));
  const Json* valid = json->find("valid");
  const Json* available = json->find("available");
  const auto v = valid ? valid->as_bool() : std::nullopt;
  const auto a = available ? available->as_bool() : std::nullopt;
  if (!v || !a) return std::unexpected(bad_response(result->status));
  return Availability{*v, *a};
}

Result<Auth> read_auth(const HttpResult& result) {
  auto json = success_body(result);
  if (!json) return std::unexpected(std::move(json.error()));
  Auth auth{string_field(*json, "token"), me_from(*json)};
  if (auth.token.empty() || auth.me.username.empty()) return std::unexpected(bad_response(result->status));
  return auth;
}

Result<Me> read_me(const HttpResult& result) {
  auto json = success_body(result);
  if (!json) return std::unexpected(std::move(json.error()));
  return me_from(*json);
}

Result<void> read_status(const HttpResult& result) {
  if (result && result->status >= 200 && result->status < 300) return {};
  // A failure's body says why; 204 from logout has none, and needs none.
  auto json = success_body(result);
  return std::unexpected(json ? bad_response(result->status) : std::move(json.error()));
}

std::optional<ServerAddress> parse_server_address(std::string_view url) {
  ServerAddress out;
  if (const auto sep = url.find("://"); sep != std::string_view::npos) {
    const std::string_view scheme = url.substr(0, sep);
    if (scheme == "http") out.secure = false;
    else if (scheme != "https") return std::nullopt;
    url.remove_prefix(sep + 3);
  }
  url = url.substr(0, url.find_first_of("?#"));
  const auto slash = url.find('/');
  std::string_view authority = url.substr(0, slash);
  out.prefix = slash == std::string_view::npos ? std::string("/") : std::string(url.substr(slash));
  if (out.prefix.back() != '/') out.prefix += '/';
  if (authority.empty() || authority.find('@') != std::string_view::npos) return std::nullopt;
  // A port follows the last ':', unless that one is inside an IPv6 address.
  const auto colon = authority.rfind(':');
  const auto bracket = authority.rfind(']');
  if (colon != std::string_view::npos && (bracket == std::string_view::npos || colon > bracket)) {
    const std::string_view digits = authority.substr(colon + 1);
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), out.port);
    if (digits.empty() || parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) return std::nullopt;
    authority = authority.substr(0, colon);
  }
  if (authority.empty()) return std::nullopt;
  out.host = std::string(authority);
  return out;
}

}  // namespace crpp::api
