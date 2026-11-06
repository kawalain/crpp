#pragma once
// Where and how to open the session WebSocket. Pure and portable.
//
// Spec: upstream client/src-tauri/src/lib.rs connect_session. The server URL
// may omit the scheme (https is assumed). http/https map to ws/wss; the path is
// the base path plus "/ws"; the nickname travels as the `nick` query parameter.
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace crpp::transport {

// The only subprotocol crpp speaks.
inline constexpr std::string_view subprotocol = "cloudroom-v2";

struct Target {
  bool secure = false;  // wss
  std::string host;     // without brackets for IPv6
  std::uint16_t port = 0;
  std::string path_and_query;  // "/ws?nick=..."
};

enum class TargetError : std::uint8_t {
  Invalid,            // not a usable URL
  UnsupportedScheme,  // not http or https
};

[[nodiscard]] std::expected<Target, TargetError> make_target(std::string_view server_url,
                                                             std::string_view nickname);

// Where upstream asks whether the server is there: the base path plus "/health".
[[nodiscard]] std::expected<Target, TargetError> make_health_target(std::string_view server_url);

}  // namespace crpp::transport
