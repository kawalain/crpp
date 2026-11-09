#include "crpp/wire/server_message.hpp"

#include <limits>

#include "json_reader.hpp"

namespace crpp::wire {

namespace {

using detail::Reader;
using Sv = std::string_view;

std::uint32_t read_u32(Reader& r) noexcept {
  const std::uint64_t v = r.u64();
  if (v > std::numeric_limits<std::uint32_t>::max()) {
    r.fail(ParseError::Number);
    return 0;
  }
  return static_cast<std::uint32_t>(v);
}

// A member's place in a Room. An input frame carries it in a byte, so no slot is more.
std::uint32_t read_slot(Reader& r) noexcept {
  const std::uint32_t v = read_u32(r);
  if (v > max_slot) r.fail(ParseError::Number);
  return v;
}

// Fields of every message type; each message picks the ones it needs.
struct Fields {
  std::optional<Sv> t, code, error, gid, ticket, key, cid, nick, emoji, text, handle;
  std::optional<std::uint32_t> me, host, slot;
  std::optional<bool> is_public, chat;
  std::optional<std::uint64_t> chat_lock, ms, events;
  std::optional<std::int64_t> time, created, stayed;
  bool has_members = false;
  std::size_t member_count = 0;
};

void read_member(Reader& r, MemberInfo& out) noexcept {
  MemberInfo m{};
  bool has_slot = false, has_nick = false, has_cid = false;
  r.begin_object();
  while (const auto key = r.next_key()) {
    if (r.null()) continue;
    if (*key == "slot") { m.slot = read_slot(r); has_slot = true; }
    else if (*key == "order") m.order = r.u64();
    else if (*key == "offline") m.offline = r.boolean();
    else if (*key == "reconnected") m.reconnected = r.boolean();
    else if (*key == "nick") { m.nick = r.string(); has_nick = true; }
    else if (*key == "cid") { m.cid = r.string(); has_cid = true; }
    else if (*key == "uid") m.uid = r.string();
    else if (*key == "emoji") m.emoji = r.string();
    else if (*key == "key") m.key = r.string();
    else if (*key == "mouse") m.mouse = r.string();
    else if (*key == "pen") m.pen = r.string();
    else r.skip();
  }
  if (!r.ok()) return;
  if (!has_slot || !has_nick || !has_cid) { r.fail(ParseError::MissingField); return; }
  out = m;
}

void read_members(Reader& r, std::span<MemberInfo> storage, Fields& f) noexcept {
  f.has_members = true;
  f.member_count = 0;
  r.begin_array();
  while (r.next_element()) {
    if (f.member_count >= storage.size()) { r.fail(ParseError::TooManyMembers); return; }
    read_member(r, storage[f.member_count]);
    if (!r.ok()) return;
    ++f.member_count;
  }
}

void read_fields(Reader& r, ParseBuffers buffers, Fields& f) noexcept {
  r.begin_object();
  while (const auto key = r.next_key()) {
    if (r.null()) continue;
    if (*key == "t") f.t = r.string();
    else if (*key == "code") f.code = r.string();
    else if (*key == "error") f.error = r.string();
    else if (*key == "gid") f.gid = r.string();
    else if (*key == "ticket") f.ticket = r.string();
    else if (*key == "key") f.key = r.string();
    else if (*key == "cid") f.cid = r.string();
    else if (*key == "nick") f.nick = r.string();
    else if (*key == "emoji") f.emoji = r.string();
    else if (*key == "text") f.text = r.string();
    else if (*key == "handle") f.handle = r.string();
    else if (*key == "me") f.me = read_slot(r);
    else if (*key == "host") f.host = read_slot(r);
    else if (*key == "slot") f.slot = read_slot(r);
    else if (*key == "public") f.is_public = r.boolean();
    else if (*key == "chat") f.chat = r.boolean();
    else if (*key == "chatLock") f.chat_lock = r.u64();
    else if (*key == "ms") f.ms = r.u64();
    else if (*key == "events") f.events = r.u64();
    else if (*key == "time") f.time = r.i64();
    else if (*key == "created") f.created = r.i64();
    else if (*key == "stayed") f.stayed = r.i64();
    else if (*key == "members") read_members(r, buffers.members, f);
    else r.skip();
  }
}

RoomState room_state(const Fields& f, const ParseBuffers& buffers) noexcept {
  return RoomState{std::span<const MemberInfo>(buffers.members.data(), f.member_count), f.host,
                   f.is_public.value_or(false), f.chat.value_or(false), f.chat_lock};
}

}  // namespace

std::expected<ServerMessage, ParseError> parse_server_message(std::string_view json,
                                                              ParseBuffers buffers) noexcept {
  Reader r(json, buffers.text);
  Fields f;
  read_fields(r, buffers, f);
  r.finish();
  if (!r.ok()) return std::unexpected(r.error());
  if (!f.t) return std::unexpected(ParseError::MissingField);

  const auto missing = std::unexpected(ParseError::MissingField);
  const Sv t = *f.t;
  if (t == "gid") {
    if (!f.gid) return missing;
    return msg::Gid{*f.gid};
  }
  if (t == "joined") {
    if (!f.code || !f.me || !f.has_members) return missing;
    return msg::Joined{*f.code, *f.me, room_state(f, buffers)};
  }
  if (t == "members") {
    if (!f.has_members) return missing;
    return msg::Members{room_state(f, buffers)};
  }
  if (t == "resume") {
    if (!f.code || !f.key) return missing;
    return msg::Resume{*f.code, *f.key};
  }
  if (t == "ticket") {
    if (!f.code || !f.ticket) return missing;
    return msg::Ticket{*f.code, *f.ticket};
  }
  if (t == "chat") {
    if (!f.slot || !f.cid || !f.nick || !f.text || !f.time) return missing;
    return msg::Chat{*f.slot, *f.cid, *f.nick, f.emoji, *f.text, *f.time};
  }
  if (t == "chat_blocked") {
    if (!f.ms) return missing;
    return msg::ChatBlocked{*f.ms};
  }
  if (t == "profile") {
    if (!f.slot || !f.nick) return missing;
    return msg::Profile{*f.slot, *f.nick, f.emoji, f.handle, f.created, f.stayed.value_or(0),
                        f.events.value_or(0)};
  }
  if (t == "left") return msg::Left{};
  if (t == "error") {
    if (!f.error) return missing;
    return msg::Error{*f.error};
  }
  return msg::Unknown{};
}

}  // namespace crpp::wire
