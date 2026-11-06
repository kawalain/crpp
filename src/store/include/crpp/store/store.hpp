#pragma once
// Everything this client keeps on disk: settings, session values, chat cache
// and held Seats. See docs/adr/0001-protected-store-with-shared-fallback.md.
//
// Store owns which store is in use and how values are split across files;
// callers only read and change values, then call save(). Nothing is written
// until then, except that a brand-new Protected store is created by open(), so
// the choice of store stays stable if upstream is installed later.
//
// Not thread-safe; use it from one thread. Times are passed in (unix ms).
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "crpp/store/json.hpp"

namespace crpp::store {

inline constexpr std::string_view default_server_url = "https://cr.nanliburusu.com/";

// How long the server holds a Seat (server/src/main.rs LEAVE_GRACE, OFFLINE_GRACE).
inline constexpr std::int64_t left_grace_ms = 5 * 60 * 1000;
inline constexpr std::int64_t offline_grace_ms = 15 * 60 * 1000;
inline constexpr std::size_t chat_log_max = 1000;
// How long a Room's chat is kept after leaving it.
inline constexpr std::int64_t chat_ttl_ms = 18 * 60 * 60 * 1000;

enum class Error : std::uint8_t {
  Protect,  // the OS refused to encrypt
  Write,    // a file could not be replaced
  BadData,  // an upstream file to change could not be read
  WrongMode,
};

enum class Mode : std::uint8_t { Protected, Shared };

struct Locations {
  std::filesystem::path protected_dir;
  std::filesystem::path shared_dir;  // upstream's folder
};

// What differs by OS, and what tests replace to inject failures.
class Platform {
 public:
  [[nodiscard]] virtual std::expected<std::string, Error> protect(std::string_view plain) = 0;
  // Protect when the data cannot be decrypted (a copy from another PC, tampering).
  [[nodiscard]] virtual std::expected<std::string, Error> unprotect(std::string_view blob) = 0;
  // Atomically replaces the file; the old content survives any failure.
  [[nodiscard]] virtual std::expected<void, Error> replace_file(const std::filesystem::path& path,
                                                                std::string_view bytes) = 0;

 protected:
  ~Platform() = default;
};

struct ChatRecord {
  std::optional<std::uint32_t> slot;
  std::string cid, nick, emoji, text;
  std::int64_t time_ms = 0;
};

// The Seat to return to if the app stops while in a Room.
struct Resume {
  std::string code, key;
  std::string ticket;  // empty if there is none for this code
  std::optional<std::int64_t> left_at;
};

struct SeatReturn {
  std::string server, owner, code, key;
  bool is_public = false;
  std::int64_t left_at = 0, expires_at = 0;
};

// The server address in the one form Seats are compared by.
[[nodiscard]] std::string normalize_server(std::string_view url);

class Store {
 public:
  [[nodiscard]] static Store open(Locations locations, Platform& platform, std::int64_t now_ms);

  [[nodiscard]] Mode mode() const noexcept { return mode_; }
  // Saved secrets could not be decrypted and were treated as absent.
  [[nodiscard]] bool secrets_lost() const noexcept { return secrets_lost_; }
  // A file did not parse. It was kept beside the original as "<name>.bad".
  [[nodiscard]] bool data_corrupt() const noexcept { return data_corrupt_; }

  // --- settings, by upstream's key names ---
  [[nodiscard]] std::string string_or(std::string_view key, std::string_view fallback) const;
  [[nodiscard]] std::int64_t i64_or(std::string_view key, std::int64_t fallback) const;
  [[nodiscard]] bool bool_or(std::string_view key, bool fallback) const;
  // A value of another shape than those, or null if there is none.
  [[nodiscard]] const Json* find(std::string_view key) const noexcept { return values_.find(key); }
  // Upstream rejects its whole file if one known key has the wrong type, so
  // keep to the types upstream uses for that key.
  void set(std::string_view key, Json value);

  // --- returning to a Seat ---
  [[nodiscard]] std::optional<Resume> resume() const;
  void set_resume(const Resume& resume);
  void clear_resume();
  void remember_seat(std::string_view code, std::string_view key, bool is_public, bool left, std::int64_t now_ms);
  void forget_seat(std::string_view code);
  [[nodiscard]] std::optional<SeatReturn> find_seat(std::string_view code, std::int64_t now_ms) const;
  [[nodiscard]] std::optional<SeatReturn> latest_seat(std::int64_t now_ms) const;

  // --- chat cache ---
  // Marks the Room as entered and returns its kept chat, oldest first.
  [[nodiscard]] std::vector<ChatRecord> enter_room(std::string_view code);
  void record_chat(std::string_view code, const ChatRecord& record);
  // The Room's chat expires chat_ttl_ms after this.
  void mark_left(std::string_view code, std::int64_t now_ms);

  // --- writing ---
  [[nodiscard]] bool has_unsaved() const noexcept { return plain_dirty_ || secret_dirty_ || session_dirty_ || chat_dirty_; }
  // Writes what changed. After an error the changes stay pending.
  [[nodiscard]] std::expected<void, Error> save();

  // --- leaving the Shared store ---
  // Copies everything into a new Protected store. On error nothing changes.
  [[nodiscard]] std::expected<void, Error> convert_to_protected();
  // Removes secrets and chat from upstream's files; upstream must sign in again.
  [[nodiscard]] std::expected<void, Error> erase_shared_secrets();

 private:
  Store(Locations locations, Platform& platform) noexcept : locations_(std::move(locations)), platform_(&platform) {}

  void load_protected();
  void load_shared();
  void prune_chat(std::int64_t now_ms);
  [[nodiscard]] std::optional<Json> read_json(const std::filesystem::path& path);
  [[nodiscard]] std::optional<Json> read_protected_json(const std::filesystem::path& path);
  [[nodiscard]] std::expected<void, Error> write(const std::filesystem::path& path, std::string_view bytes);
  [[nodiscard]] std::expected<void, Error> save_protected();
  [[nodiscard]] std::expected<void, Error> save_shared();
  [[nodiscard]] std::string seat_owner() const;
  [[nodiscard]] std::vector<SeatReturn> live_seats(std::int64_t now_ms) const;
  void store_seats(const std::vector<SeatReturn>& seats);
  [[nodiscard]] Json& room(std::string_view code);

  Locations locations_;
  Platform* platform_;
  Mode mode_ = Mode::Protected;
  bool secrets_lost_ = false;
  bool data_corrupt_ = false;
  bool plain_dirty_ = false, secret_dirty_ = false, session_dirty_ = false, chat_dirty_ = false;

  Json values_ = Json::object();  // settings and session values, by upstream's keys
  Json rooms_ = Json::object();   // code -> {log, leftAt}
  // Parts of upstream's cache file this client does not read.
  Json cache_rest_ = Json::object();
  Json cache_session_rest_ = Json::object();
};

}  // namespace crpp::store
