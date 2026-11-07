#include "crpp/store/store.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

namespace crpp::store {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t max_file_bytes = 64 * 1024 * 1024;

// In upstream's cache file rather than settings.json. Secret in both stores.
constexpr std::array<std::string_view, 5> session_keys = {"resumeCode", "resumeKey", "roomTicket", "leftAt", "seatReturns"};
// In settings.json upstream, but encrypted in the Protected store.
constexpr std::array<std::string_view, 2> account_secret_keys = {"token", "gid"};

bool is_session_key(std::string_view key) { return std::ranges::find(session_keys, key) != session_keys.end(); }
bool is_account_secret(std::string_view key) { return std::ranges::find(account_secret_keys, key) != account_secret_keys.end(); }

fs::path plus_suffix(fs::path path, const char* suffix) {
  path += suffix;
  return path;
}

std::optional<std::string> read_file(const fs::path& path) {
  std::error_code ec;
  const auto size = fs::file_size(path, ec);
  if (ec || size > max_file_bytes) return std::nullopt;
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Upstream's cache encoding. Only hides text from a casual look; applying it
// twice restores the input.
std::string scramble(std::string data) {
  std::uint32_t state = 0x9E3779B9;
  for (char& byte : data) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    byte = static_cast<char>(static_cast<unsigned char>(byte) ^ static_cast<unsigned char>(state >> 8));
  }
  return data;
}

char lower(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool valid_code(std::string_view code) {
  return code.size() == 6 && std::ranges::all_of(code, [](char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '2' && c <= '9');
  });
}

Json seat_to_json(const SeatReturn& s) {
  Json o = Json::object();
  o.set("server", Json(s.server));
  o.set("owner", Json(s.owner));
  o.set("code", Json(s.code));
  o.set("key", Json(s.key));
  o.set("public", Json(s.is_public));
  o.set("leftAt", Json(s.left_at));
  o.set("expiresAt", Json(s.expires_at));
  return o;
}

std::optional<SeatReturn> seat_from_json(const Json& o) {
  const auto text = [&](std::string_view key) -> std::string {
    const Json* v = o.find(key);
    const auto s = v ? v->as_string() : std::nullopt;
    return s ? std::string(*s) : std::string{};
  };
  SeatReturn seat{text("server"), text("owner"), text("code"), text("key"), false, 0, 0};
  const Json* expires = o.find("expiresAt");
  const auto expires_at = expires ? expires->as_i64() : std::nullopt;
  if (seat.key.empty() || !valid_code(seat.code) || !expires_at) return std::nullopt;
  seat.expires_at = *expires_at;
  if (const Json* v = o.find("public")) seat.is_public = v->as_bool().value_or(false);
  if (const Json* v = o.find("leftAt")) seat.left_at = v->as_i64().value_or(0);
  return seat;
}

Json chat_to_json(const ChatRecord& r) {
  Json o = Json::object();
  o.set("slot", r.slot ? Json(*r.slot) : Json{});
  o.set("cid", Json(r.cid));
  o.set("nick", Json(r.nick));
  o.set("emoji", r.emoji.empty() ? Json{} : Json(r.emoji));
  o.set("text", Json(r.text));
  o.set("time", Json(r.time_ms));
  return o;
}

ChatRecord chat_from_json(const Json& o) {
  const auto text = [&](std::string_view key) -> std::string {
    const Json* v = o.find(key);
    const auto s = v ? v->as_string() : std::nullopt;
    return s ? std::string(*s) : std::string{};
  };
  ChatRecord r{std::nullopt, text("cid"), text("nick"), text("emoji"), text("text"), 0};
  if (const Json* v = o.find("slot")) {
    if (const auto slot = v->as_i64(); slot && *slot >= 0) r.slot = static_cast<std::uint32_t>(*slot);
  }
  if (const Json* v = o.find("time")) r.time_ms = v->as_i64().value_or(0);
  return r;
}

}  // namespace

std::string normalize_server(std::string_view url) {
  while (!url.empty() && (url.front() == ' ' || url.front() == '\t')) url.remove_prefix(1);
  while (!url.empty() && (url.back() == ' ' || url.back() == '\t' || url.back() == '/')) url.remove_suffix(1);
  std::string out;
  if (url.find("://") == std::string_view::npos) {
    const bool local = url.starts_with("localhost") || url.starts_with("127.0.0.1") || url.starts_with("[::1]");
    out = local ? "http://" : "https://";
  }
  out += url;
  // The scheme and host are case-insensitive; a path is not.
  const std::size_t host = out.find("://") + 3;
  const std::size_t host_end = std::min(out.find('/', host), out.size());
  std::transform(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(host_end), out.begin(), lower);
  return out;
}

// ---------------------------------------------------------------- open

Store Store::open(Locations locations, Platform& platform, std::int64_t now_ms) {
  Store store(std::move(locations), platform);
  std::error_code ec;
  if (fs::is_regular_file(store.locations_.protected_dir / "settings.json", ec)) {
    store.load_protected();
  } else if (fs::is_regular_file(store.locations_.shared_dir / "settings.json", ec)) {
    store.mode_ = Mode::Shared;
    store.load_shared();
  } else {
    store.plain_dirty_ = store.secret_dirty_ = store.session_dirty_ = store.chat_dirty_ = true;
    (void)store.save();  // a failure stays pending and is reported by a later save
  }
  store.prune_chat(now_ms);
  return store;
}

std::optional<Json> Store::read_json(const fs::path& path) {
  const auto text = read_file(path);
  if (!text) return std::nullopt;
  auto parsed = Json::parse(*text);
  if (parsed && parsed->is_object()) return std::move(*parsed);
  std::error_code ec;
  fs::rename(path, plus_suffix(path, ".bad"), ec);
  data_corrupt_ = true;
  return std::nullopt;
}

std::optional<Json> Store::read_protected_json(const fs::path& path) {
  const auto blob = read_file(path);
  if (!blob) return std::nullopt;
  const auto plain = platform_->unprotect(*blob);
  // Decryption can succeed on tampered data, so a parse failure counts as loss.
  auto parsed = plain ? Json::parse(*plain) : std::unexpected(JsonError::Syntax);
  if (parsed && parsed->is_object()) return std::move(*parsed);
  secrets_lost_ = true;
  return std::nullopt;
}

void Store::load_protected() {
  if (auto settings = read_json(locations_.protected_dir / "settings.json")) values_ = std::move(*settings);
  if (auto secrets = read_protected_json(locations_.protected_dir / "secrets.bin")) {
    if (const auto* members = secrets->as_object()) {
      for (const auto& member : *members) values_.set(member.key, member.value);
    }
  }
  if (auto rooms = read_protected_json(locations_.protected_dir / "chat.bin")) rooms_ = std::move(*rooms);
}

void Store::load_shared() {
  if (auto settings = read_json(locations_.shared_dir / "settings.json")) values_ = std::move(*settings);
  const fs::path cache = locations_.shared_dir / "cache" / "cache";
  const auto bytes = read_file(cache);
  if (!bytes) return;
  auto parsed = Json::parse(scramble(*bytes));
  if (!parsed || !parsed->is_object()) {
    std::error_code ec;
    fs::rename(cache, plus_suffix(cache, ".bad"), ec);
    data_corrupt_ = true;
    return;
  }
  if (auto rooms = parsed->take("rooms"); rooms && rooms->is_object()) rooms_ = std::move(*rooms);
  if (auto session = parsed->take("session"); session && session->is_object()) {
    for (const auto key : session_keys) {
      if (auto value = session->take(key)) values_.set(key, std::move(*value));
    }
    cache_session_rest_ = std::move(*session);
  }
  cache_rest_ = std::move(*parsed);
}

// ------------------------------------------------------------ plain values

std::string Store::string_or(std::string_view key, std::string_view fallback) const {
  const Json* v = values_.find(key);
  const auto s = v ? v->as_string() : std::nullopt;
  return std::string(s.value_or(fallback));
}

std::int64_t Store::i64_or(std::string_view key, std::int64_t fallback) const {
  const Json* v = values_.find(key);
  return v ? v->as_i64().value_or(fallback) : fallback;
}

bool Store::bool_or(std::string_view key, bool fallback) const {
  const Json* v = values_.find(key);
  return v ? v->as_bool().value_or(fallback) : fallback;
}

void Store::set(std::string_view key, Json value) {
  if (const Json* existing = values_.find(key); existing && *existing == value) return;
  values_.set(key, std::move(value));
  if (is_session_key(key)) session_dirty_ = true;
  else if (is_account_secret(key)) secret_dirty_ = true;
  else plain_dirty_ = true;
}

// ------------------------------------------------------------------ Seats

std::optional<Resume> Store::resume() const {
  Resume r{string_or("resumeCode", ""), string_or("resumeKey", ""), {}, std::nullopt};
  if (r.code.empty() || r.key.empty()) return std::nullopt;
  if (const Json* ticket = values_.find("roomTicket")) {
    const Json* code = ticket->find("code");
    const Json* token = ticket->find("token");
    if (code && token && code->as_string() == r.code) r.ticket = std::string(token->as_string().value_or(""));
  }
  if (const Json* left = values_.find("leftAt")) r.left_at = left->as_i64();
  return r;
}

void Store::set_resume(const Resume& resume) {
  set("resumeCode", Json(resume.code));
  set("resumeKey", Json(resume.key));
  set("leftAt", resume.left_at ? Json(*resume.left_at) : Json{});
  if (resume.ticket.empty()) {
    set("roomTicket", Json{});
  } else {
    Json ticket = Json::object();
    ticket.set("code", Json(resume.code));
    ticket.set("token", Json(resume.ticket));
    set("roomTicket", std::move(ticket));
  }
}

void Store::clear_resume() {
  for (const auto key : {"resumeCode", "resumeKey", "roomTicket", "leftAt"}) {
    if (const Json* v = values_.find(key); v && !v->is_null()) set(key, Json{});
  }
}

std::string Store::seat_owner() const {
  if (bool_or("isGuest", true)) return "guest";
  return string_or("username", "");
}

std::vector<SeatReturn> Store::live_seats(std::int64_t now_ms) const {
  std::vector<SeatReturn> seats;
  const Json* list = values_.find("seatReturns");
  const auto* items = list ? list->as_array() : nullptr;
  if (!items) return seats;
  for (const Json& item : *items) {
    if (auto seat = seat_from_json(item); seat && seat->expires_at > now_ms) seats.push_back(std::move(*seat));
  }
  return seats;
}

void Store::store_seats(const std::vector<SeatReturn>& seats) {
  Json::Array items;
  items.reserve(seats.size());
  for (const SeatReturn& seat : seats) items.push_back(seat_to_json(seat));
  set("seatReturns", Json(std::move(items)));
}

void Store::remember_seat(std::string_view code, std::string_view key, bool is_public, bool left, std::int64_t now_ms) {
  if (code.empty() || key.empty()) return;
  const std::string server = normalize_server(string_or("serverUrl", default_server_url));
  const std::string owner = seat_owner();
  auto seats = live_seats(now_ms);
  std::erase_if(seats, [&](const SeatReturn& s) { return s.server == server && s.owner == owner && s.code == code; });
  seats.push_back({server, owner, std::string(code), std::string(key), is_public, now_ms,
                   now_ms + (left ? left_grace_ms : offline_grace_ms)});
  store_seats(seats);
}

void Store::forget_seat(std::string_view code) {
  const Json* list = values_.find("seatReturns");
  const auto* items = list ? list->as_array() : nullptr;
  if (!items) return;
  const std::string server = normalize_server(string_or("serverUrl", default_server_url));
  const std::string owner = seat_owner();
  std::vector<SeatReturn> seats;
  bool found = false;
  for (const Json& item : *items) {
    auto seat = seat_from_json(item);
    if (!seat) continue;
    if (seat->server == server && seat->owner == owner && seat->code == code) found = true;
    else seats.push_back(std::move(*seat));
  }
  if (found) store_seats(seats);
}

std::optional<SeatReturn> Store::find_seat(std::string_view code, std::int64_t now_ms) const {
  const std::string server = normalize_server(string_or("serverUrl", default_server_url));
  const std::string owner = seat_owner();
  for (auto& seat : live_seats(now_ms)) {
    if (seat.server == server && seat.owner == owner && seat.code == code) return std::move(seat);
  }
  return std::nullopt;
}

std::vector<std::string> Store::seat_codes(std::int64_t now_ms) const {
  const std::string server = normalize_server(string_or("serverUrl", default_server_url));
  const std::string owner = seat_owner();
  std::vector<SeatReturn> seats = live_seats(now_ms);
  std::erase_if(seats, [&](const SeatReturn& seat) { return seat.server != server || seat.owner != owner; });
  std::ranges::stable_sort(seats, std::greater{}, &SeatReturn::left_at);
  std::vector<std::string> codes;
  codes.reserve(seats.size());
  for (SeatReturn& seat : seats) codes.push_back(std::move(seat.code));
  return codes;
}

std::optional<SeatReturn> Store::latest_seat(std::int64_t now_ms) const {
  const std::string server = normalize_server(string_or("serverUrl", default_server_url));
  const std::string owner = seat_owner();
  std::optional<SeatReturn> best;
  for (auto& seat : live_seats(now_ms)) {
    if (seat.server != server || seat.owner != owner) continue;
    if (!best || seat.left_at > best->left_at) best = std::move(seat);
  }
  return best;
}

// ------------------------------------------------------------- chat cache

Json& Store::room(std::string_view code) {
  if (Json* existing = rooms_.find(code)) return *existing;
  Json fresh = Json::object();
  fresh.set("log", Json::array());
  fresh.set("leftAt", Json{});
  rooms_.set(code, std::move(fresh));
  return *rooms_.find(code);
}

void Store::prune_chat(std::int64_t now_ms) {
  const auto* members = rooms_.as_object();
  if (!members) return;
  Json kept = Json::object();
  for (const auto& member : *members) {
    Json entry = member.value;
    const Json* left = entry.find("leftAt");
    const auto left_at = left ? left->as_i64() : std::nullopt;
    // A Room with no leaving time was open when the app last stopped.
    if (!left_at) {
      entry.set("leftAt", Json(now_ms));
      chat_dirty_ = true;
    } else if (now_ms - *left_at > chat_ttl_ms) {
      chat_dirty_ = true;
      continue;
    }
    kept.set(member.key, std::move(entry));
  }
  rooms_ = std::move(kept);
}

std::vector<ChatRecord> Store::enter_room(std::string_view code) {
  Json& entry = room(code);
  entry.set("leftAt", Json{});
  chat_dirty_ = true;
  std::vector<ChatRecord> history;
  const Json* log = entry.find("log");
  if (const auto* items = log ? log->as_array() : nullptr) {
    history.reserve(items->size());
    for (const Json& item : *items) history.push_back(chat_from_json(item));
  }
  return history;
}

void Store::record_chat(std::string_view code, const ChatRecord& record) {
  if (code.empty() || record.text.empty()) return;
  Json& entry = room(code);
  Json* log = entry.find("log");
  if (!log || !log->as_array()) {
    entry.set("log", Json::array());
    log = entry.find("log");
  }
  auto* items = log->as_array();
  items->push_back(chat_to_json(record));
  if (items->size() > chat_log_max) items->erase(items->begin(), items->end() - chat_log_max);
  chat_dirty_ = true;
}

void Store::clear_chat() {
  rooms_ = Json::object();
  chat_dirty_ = true;
}

void Store::mark_left(std::string_view code, std::int64_t now_ms) {
  if (Json* entry = rooms_.find(code)) {
    entry->set("leftAt", Json(now_ms));
    chat_dirty_ = true;
  }
}

// ---------------------------------------------------------------- writing

std::expected<void, Error> Store::write(const fs::path& path, std::string_view bytes) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);  // a failure surfaces in replace_file
  return platform_->replace_file(path, bytes);
}

std::expected<void, Error> Store::save() {
  return mode_ == Mode::Protected ? save_protected() : save_shared();
}

std::expected<void, Error> Store::save_protected() {
  // settings.json marks the store as existing, so it is written last: an
  // interrupted first save leaves no half-made store behind.
  const auto sealed = [&](const char* name, const Json& value) -> std::expected<void, Error> {
    auto blob = platform_->protect(value.dump());
    if (!blob) return std::unexpected(blob.error());
    return write(locations_.protected_dir / name, *blob);
  };
  if (secret_dirty_ || session_dirty_) {
    Json secrets = Json::object();
    if (const auto* members = values_.as_object()) {
      for (const auto& member : *members) {
        if (is_session_key(member.key) || is_account_secret(member.key)) secrets.set(member.key, member.value);
      }
    }
    if (auto done = sealed("secrets.bin", secrets); !done) return done;
    secret_dirty_ = session_dirty_ = false;
  }
  if (chat_dirty_) {
    if (auto done = sealed("chat.bin", rooms_); !done) return done;
    chat_dirty_ = false;
  }
  if (plain_dirty_) {
    Json plain = Json::object();
    if (const auto* members = values_.as_object()) {
      for (const auto& member : *members) {
        if (!is_session_key(member.key) && !is_account_secret(member.key)) plain.set(member.key, member.value);
      }
    }
    if (auto done = write(locations_.protected_dir / "settings.json", plain.dump(true)); !done) return done;
    plain_dirty_ = false;
  }
  return {};
}

std::expected<void, Error> Store::save_shared() {
  if (plain_dirty_ || secret_dirty_) {
    Json settings = Json::object();
    if (const auto* members = values_.as_object()) {
      for (const auto& member : *members) {
        if (!is_session_key(member.key)) settings.set(member.key, member.value);
      }
    }
    if (auto done = write(locations_.shared_dir / "settings.json", settings.dump(true)); !done) return done;
    plain_dirty_ = secret_dirty_ = false;
  }
  if (session_dirty_ || chat_dirty_) {
    Json session = cache_session_rest_;
    for (const auto key : session_keys) {
      if (const Json* value = values_.find(key)) session.set(key, *value);
    }
    Json cache = cache_rest_;
    cache.set("rooms", std::move(rooms_));
    cache.set("session", std::move(session));
    const std::string bytes = scramble(cache.dump());
    rooms_ = std::move(*cache.take("rooms"));
    if (auto done = write(locations_.shared_dir / "cache" / "cache", bytes); !done) return done;
    session_dirty_ = chat_dirty_ = false;
  }
  return {};
}

std::expected<void, Error> Store::convert_to_protected() {
  if (mode_ != Mode::Shared) return std::unexpected(Error::WrongMode);
  const bool plain = plain_dirty_, secret = secret_dirty_, session = session_dirty_, chat = chat_dirty_;
  mode_ = Mode::Protected;
  plain_dirty_ = secret_dirty_ = session_dirty_ = chat_dirty_ = true;
  auto done = save_protected();
  if (!done) {
    mode_ = Mode::Shared;
    plain_dirty_ = plain;
    secret_dirty_ = secret;
    session_dirty_ = session;
    chat_dirty_ = chat;
  }
  return done;
}

std::expected<void, Error> Store::erase_shared_secrets() {
  if (mode_ != Mode::Protected) return std::unexpected(Error::WrongMode);
  const fs::path settings_path = locations_.shared_dir / "settings.json";
  const auto text = read_file(settings_path);
  if (!text) return std::unexpected(Error::BadData);
  auto settings = Json::parse(*text);
  if (!settings || !settings->is_object()) return std::unexpected(Error::BadData);

  // The cache goes first: if the second write fails, retrying still works.
  const fs::path cache_path = locations_.shared_dir / "cache" / "cache";
  Json cache = Json::object();
  if (const auto bytes = read_file(cache_path)) {
    if (auto parsed = Json::parse(scramble(*bytes)); parsed && parsed->is_object()) cache = std::move(*parsed);
  }
  cache.set("rooms", Json::object());
  cache.set("session", Json::object());
  if (auto done = write(cache_path, scramble(cache.dump())); !done) return done;

  for (const auto key : account_secret_keys) settings->set(key, Json{});
  for (const auto key : session_keys) settings->erase(key);  // an old upstream kept them here
  if (auto done = write(settings_path, settings->dump(true)); !done) return done;

  // An even older upstream kept session values in this file.
  std::error_code ec;
  fs::remove(locations_.shared_dir / "session.json", ec);
  return {};
}

}  // namespace crpp::store
