#pragma once
// Text (JSON) messages the server sends, parsed without allocating.
// Spec: upstream server/src/main.rs (ServerMessage, MemberInfo and the
// serde_json::json! messages). `null` and an absent field mean the same.
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

#include "crpp/wire/parse_error.hpp"

namespace crpp::wire {

struct MemberInfo {
  std::uint32_t slot{};
  std::uint64_t order{};
  bool offline{};
  bool reconnected{};
  std::string_view nick;
  std::string_view cid;
  std::optional<std::string_view> uid;
  std::optional<std::string_view> emoji;
  std::optional<std::string_view> key;
  std::optional<std::string_view> mouse;
  std::optional<std::string_view> pen;
};

struct RoomState {
  std::span<const MemberInfo> members;
  std::optional<std::uint32_t> host;
  bool is_public{};
  bool chat{};
  std::optional<std::uint64_t> chat_lock;
};

namespace msg {

struct Gid { std::string_view gid; };
struct Joined { std::string_view code; std::uint32_t me{}; RoomState room; };
struct Members { RoomState room; };
struct Resume { std::string_view code; std::string_view key; };
struct Ticket { std::string_view code; std::string_view ticket; };
struct Chat {
  std::uint32_t slot{};
  std::string_view cid;
  std::string_view nick;
  std::optional<std::string_view> emoji;
  std::string_view text;
  std::int64_t time{};  // unix ms
};
struct ChatBlocked { std::uint64_t ms{}; };
struct Profile {
  std::uint32_t slot{};
  std::string_view nick;
  std::optional<std::string_view> emoji;
  std::optional<std::string_view> handle;
  std::optional<std::int64_t> created;
  std::int64_t stayed{};
  std::uint64_t events{};
};
struct Left {};
struct Error { std::string_view error; };
struct Unknown {};  // a well-formed message with a type this client ignores

}  // namespace msg

using ServerMessage =
    std::variant<msg::Gid, msg::Joined, msg::Members, msg::Resume, msg::Ticket, msg::Chat,
                 msg::ChatBlocked, msg::Profile, msg::Left, msg::Error, msg::Unknown>;

// Caller-provided storage. `text` must be at least as large as the JSON input
// (decoding never grows a string); `members` bounds the room size.
struct ParseBuffers {
  std::span<char> text;
  std::span<MemberInfo> members;
};

// Every string in the result points into buffers.text and every member into
// buffers.members; nothing points into `json`. The result is valid until the
// buffers are reused.
[[nodiscard]] std::expected<ServerMessage, ParseError> parse_server_message(
    std::string_view json, ParseBuffers buffers) noexcept;

}  // namespace crpp::wire
