#pragma once
// Text (JSON) messages the client sends, encoded without allocating.
// Absent optional fields are omitted (the server reads them as None).
// Spec: upstream server/src/main.rs (ClientMessage and its dispatch).
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace crpp::wire {

// The server drops text frames larger than this.
inline constexpr std::size_t max_text_frame_bytes = 1024;

namespace cmd {

struct Create { bool is_public{}; bool chat{}; std::optional<std::string_view> gid; };
struct Join { std::string_view code; std::optional<std::string_view> gid; };
struct JoinRandom { std::optional<std::string_view> gid; };
struct Resume {
  std::string_view code;
  std::string_view key;
  std::optional<std::string_view> ticket;
  std::optional<std::string_view> gid;
};
struct Visibility { bool is_public{}; };
struct ChatEnable {};
struct ChatDisable {};
struct Chat { std::string_view text; };
struct Profile { std::uint32_t slot{}; };
struct Nick { std::string_view nick; };
struct Sounds {
  std::optional<std::string_view> key;
  std::optional<std::string_view> mouse;
  std::optional<std::string_view> pen;
  std::optional<std::string_view> emoji;
};
struct Leave {};

}  // namespace cmd

using ClientMessage =
    std::variant<cmd::Create, cmd::Join, cmd::JoinRandom, cmd::Resume, cmd::Visibility,
                 cmd::ChatEnable, cmd::ChatDisable, cmd::Chat, cmd::Profile, cmd::Nick,
                 cmd::Sounds, cmd::Leave>;

enum class EncodeError : std::uint8_t {
  BufferTooSmall,  // out cannot hold the message
  TooLong,         // the message exceeds max_text_frame_bytes
  InvalidUtf8,     // a string argument is not valid UTF-8
};

// Writes the JSON text into out and returns its length. Strings must be valid
// UTF-8; control characters, quotes and backslashes are escaped.
[[nodiscard]] std::expected<std::size_t, EncodeError> encode_client_message(
    const ClientMessage& message, std::span<char> out) noexcept;

}  // namespace crpp::wire
