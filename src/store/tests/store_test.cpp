#include "crpp/store/store.hpp"

#include <fstream>
#include <iterator>
#include <random>
#include <string>

#include "check.hpp"

using namespace crpp::store;
namespace fs = std::filesystem;

namespace {

constexpr std::int64_t t0 = 1'700'000'000'000;

// Reversible, and visibly not the plain text. Failures can be switched on.
class FakePlatform final : public Platform {
 public:
  bool fail_protect = false, fail_unprotect = false, fail_write = false;
  std::expected<std::string, Error> protect(std::string_view plain) override {
    if (fail_protect) return std::unexpected(Error::Protect);
    std::string out = "SEALED";
    for (const char c : plain) out += static_cast<char>(c ^ 0x5A);
    return out;
  }
  std::expected<std::string, Error> unprotect(std::string_view blob) override {
    if (fail_unprotect || !blob.starts_with("SEALED")) return std::unexpected(Error::Protect);
    std::string out;
    for (const char c : blob.substr(6)) out += static_cast<char>(c ^ 0x5A);
    return out;
  }
  std::expected<void, Error> replace_file(const fs::path& path, std::string_view bytes) override {
    if (fail_write) return std::unexpected(Error::Write);
    std::ofstream(path, std::ios::binary) << bytes;
    return {};
  }
};

class TempDirs {
 public:
  TempDirs() {
    std::random_device rd;
    root_ = fs::temp_directory_path() / ("crpp_store_test_" + std::to_string(rd()) + std::to_string(rd()));
    fs::create_directories(root_);
  }
  ~TempDirs() {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }
  [[nodiscard]] Locations locations() const { return {root_ / "crpp", root_ / "Cloudroom"}; }
  [[nodiscard]] fs::path mine() const { return root_ / "crpp"; }
  [[nodiscard]] fs::path upstream() const { return root_ / "Cloudroom"; }

 private:
  fs::path root_;
};

std::string read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write(const fs::path& path, std::string_view text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

std::string mix(std::string data) {
  std::uint32_t state = 0x9E3779B9;
  for (char& byte : data) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    byte = static_cast<char>(static_cast<unsigned char>(byte) ^ static_cast<unsigned char>(state >> 8));
  }
  return data;
}

std::string hex(std::string_view bytes) {
  constexpr char digits[] = "0123456789abcdef";
  std::string out;
  for (const char c : bytes) {
    out += digits[(static_cast<unsigned char>(c) >> 4) & 0xF];
    out += digits[static_cast<unsigned char>(c) & 0xF];
  }
  return out;
}

void write_upstream(const TempDirs& dirs) {
  write(dirs.upstream() / "settings.json",
        R"({"serverUrl":"https://cr.nanliburusu.com/","token":"tok","isGuest":false,"username":"me","nickname":"구름","gid":"g1","futureKey":{"x":[1.50]}})");
  write(dirs.upstream() / "cache" / "cache",
        mix(R"({"rooms":{"ABC234":{"log":[{"slot":1,"cid":"c","nick":"n","emoji":null,"text":"hi","time":5,"extra":1}],"leftAt":null}},"session":{"resumeCode":"ABC234","resumeKey":"k1","roomTicket":{"code":"ABC234","token":"t1"},"leftAt":null,"seatReturns":[],"futureSession":7},"futureTop":"x"})"));
}

// --- which store ---

void new_user_gets_a_protected_store() {
  TempDirs dirs;
  FakePlatform platform;
  auto store = Store::open(dirs.locations(), platform, t0);
  CHECK(store.mode() == Mode::Protected);
  CHECK(fs::exists(dirs.mine() / "settings.json"));
  CHECK(!fs::exists(dirs.upstream()));
  CHECK(!store.has_unsaved());
}

void existing_upstream_files_select_the_shared_store() {
  TempDirs dirs;
  FakePlatform platform;
  write_upstream(dirs);
  const auto store = Store::open(dirs.locations(), platform, t0);
  CHECK(store.mode() == Mode::Shared);
  CHECK(store.string_or("nickname", "") == "구름");
  CHECK(store.string_or("token", "") == "tok");
  CHECK(!fs::exists(dirs.mine()));
  const auto resume = store.resume();
  CHECK(resume && resume->code == "ABC234" && resume->key == "k1" && resume->ticket == "t1" && !resume->left_at);
}

void protected_store_wins_when_upstream_arrives_later() {
  TempDirs dirs;
  FakePlatform platform;
  {
    auto store = Store::open(dirs.locations(), platform, t0);
    store.set("nickname", Json("mine"));
    CHECK(store.save().has_value());
  }
  write_upstream(dirs);
  const auto store = Store::open(dirs.locations(), platform, t0);
  CHECK(store.mode() == Mode::Protected);
  CHECK(store.string_or("nickname", "") == "mine");
  CHECK(store.string_or("token", "none") == "none");
}

// --- Protected store ---

void secrets_never_reach_a_plain_file() {
  TempDirs dirs;
  FakePlatform platform;
  auto store = Store::open(dirs.locations(), platform, t0);
  store.set("token", Json("SECRET-token"));
  store.set("gid", Json("SECRET-gid"));
  store.set("volume", Json(30));
  store.set_resume({"ABC234", "SECRET-key", "SECRET-ticket", std::nullopt});
  store.remember_seat("XYZ234", "SECRET-seat", false, true, t0);
  store.record_chat("ABC234", {1, "c", "n", "", "SECRET-chat", 5});
  CHECK(store.save().has_value());

  for (const auto& entry : fs::recursive_directory_iterator(dirs.mine())) {
    CHECK(read(entry.path()).find("SECRET") == std::string::npos);
  }
  CHECK(read(dirs.mine() / "settings.json").find("volume") != std::string::npos);

  auto again = Store::open(dirs.locations(), platform, t0);
  CHECK(again.string_or("token", "") == "SECRET-token");
  CHECK(again.string_or("gid", "") == "SECRET-gid");
  CHECK(again.i64_or("volume", 0) == 30);
  CHECK(again.resume()->ticket == "SECRET-ticket");
  CHECK(again.find_seat("XYZ234", t0)->key == "SECRET-seat");
  CHECK(again.enter_room("ABC234").at(0).text == "SECRET-chat");
}

void unreadable_secrets_count_as_absent() {
  TempDirs dirs;
  FakePlatform platform;
  {
    auto store = Store::open(dirs.locations(), platform, t0);
    store.set("token", Json("tok"));
    store.set("nickname", Json("n"));
    CHECK(store.save().has_value());
  }
  platform.fail_unprotect = true;  // as on another PC
  {
    auto store = Store::open(dirs.locations(), platform, t0);
    CHECK(store.secrets_lost());
    CHECK(store.string_or("token", "none") == "none");
    CHECK(store.string_or("nickname", "") == "n");
    store.set("gid", Json("g"));
    platform.fail_unprotect = false;
    CHECK(store.save().has_value());
  }
  const auto store = Store::open(dirs.locations(), platform, t0);
  CHECK(!store.secrets_lost());
  CHECK(store.string_or("gid", "") == "g");
}

void tampered_secrets_count_as_absent() {
  TempDirs dirs;
  FakePlatform platform;
  {
    auto store = Store::open(dirs.locations(), platform, t0);
    store.set("token", Json("tok"));
    CHECK(store.save().has_value());
  }
  write(dirs.mine() / "secrets.bin", "SEALEDgarbage");
  const auto store = Store::open(dirs.locations(), platform, t0);
  CHECK(store.secrets_lost());
  CHECK(store.string_or("token", "none") == "none");
}

void corrupt_settings_are_kept_aside() {
  TempDirs dirs;
  FakePlatform platform;
  { (void)Store::open(dirs.locations(), platform, t0); }
  write(dirs.mine() / "settings.json", "{ not json");
  auto store = Store::open(dirs.locations(), platform, t0);
  CHECK(store.data_corrupt());
  CHECK(read(dirs.mine() / "settings.json.bad") == "{ not json");
  store.set("nickname", Json("n"));
  CHECK(store.save().has_value());
  CHECK(!Store::open(dirs.locations(), platform, t0).data_corrupt());
}

void failed_saves_stay_pending() {
  TempDirs dirs;
  FakePlatform platform;
  auto store = Store::open(dirs.locations(), platform, t0);
  store.set("token", Json("tok"));
  store.set("nickname", Json("n"));
  platform.fail_protect = true;
  CHECK(!store.save().has_value());
  CHECK(store.has_unsaved());
  platform.fail_protect = false;
  platform.fail_write = true;
  CHECK(store.save().error() == Error::Write);
  CHECK(store.has_unsaved());
  platform.fail_write = false;
  CHECK(store.save().has_value());
  CHECK(!store.has_unsaved());
  CHECK(Store::open(dirs.locations(), platform, t0).string_or("token", "") == "tok");
}

void unchanged_values_write_nothing() {
  TempDirs dirs;
  FakePlatform platform;
  auto store = Store::open(dirs.locations(), platform, t0);
  store.set("nickname", Json("n"));
  CHECK(store.save().has_value());
  store.set("nickname", Json("n"));
  CHECK(!store.has_unsaved());
}

void failed_first_save_leaves_no_store() {
  TempDirs dirs;
  FakePlatform platform;
  platform.fail_write = true;
  auto store = Store::open(dirs.locations(), platform, t0);
  CHECK(store.has_unsaved());
  CHECK(store.string_or("nickname", "fine") == "fine");
  CHECK(!fs::exists(dirs.mine() / "settings.json"));
}

// --- Shared store ---

void shared_files_keep_upstream_format_and_unknown_values() {
  TempDirs dirs;
  FakePlatform platform;
  write_upstream(dirs);
  auto store = Store::open(dirs.locations(), platform, t0);
  store.set("nickname", Json("새 이름"));
  store.set("gid", Json("g2"));
  store.set_resume({"XYZ234", "k2", "t2", std::nullopt});
  store.record_chat("XYZ234", {2, "c", "n", "1f600", "hello", 9});
  CHECK(store.save().has_value());

  const auto settings = Json::parse(read(dirs.upstream() / "settings.json"));
  CHECK(settings.has_value());
  CHECK(settings->find("nickname")->as_string() == "새 이름");
  CHECK(settings->find("gid")->as_string() == "g2");
  CHECK(settings->find("token")->as_string() == "tok");
  CHECK(settings->find("futureKey")->find("x")->dump() == "[1.50]");
  CHECK(settings->find("resumeKey") == nullptr);  // session values live in the cache

  const auto cache = Json::parse(mix(read(dirs.upstream() / "cache" / "cache")));
  CHECK(cache.has_value());
  CHECK(cache->find("futureTop")->as_string() == "x");
  const Json* session = cache->find("session");
  CHECK(session->find("resumeKey")->as_string() == "k2");
  CHECK(session->find("roomTicket")->find("token")->as_string() == "t2");
  CHECK(session->find("futureSession")->as_i64() == 7);
  CHECK(session->find("leftAt")->is_null());
  const Json* log = cache->find("rooms")->find("XYZ234")->find("log");
  CHECK(log->as_array()->size() == 1);
  CHECK((*log->as_array())[0].dump() == R"({"slot":2,"cid":"c","nick":"n","emoji":"1f600","text":"hello","time":9})");
  CHECK(cache->find("rooms")->find("ABC234")->find("log")->as_array()->at(0).find("extra")->as_i64() == 1);
  CHECK(!fs::exists(dirs.mine()));
}

void cache_encoding_matches_upstream() {
  TempDirs dirs;
  FakePlatform platform;
  write(dirs.upstream() / "settings.json", "{}");
  auto store = Store::open(dirs.locations(), platform, t0);
  store.set("resumeCode", Json{});
  CHECK(store.save().has_value());
  // Golden bytes for {"rooms":{},"session":{}} from an independent implementation.
  CHECK(hex(mix(R"({"rooms":{},"session":{}})")) == "3d77fdc702bf5acfd428d484aa22153599a9f3276ed3d58984");
  const auto cache = Json::parse(mix(read(dirs.upstream() / "cache" / "cache")));
  CHECK(cache && cache->find("session")->find("resumeCode")->is_null());
}

void corrupt_cache_is_kept_aside() {
  TempDirs dirs;
  FakePlatform platform;
  write(dirs.upstream() / "settings.json", R"({"nickname":"n"})");
  write(dirs.upstream() / "cache" / "cache", "\x01\x02\x03");
  const auto store = Store::open(dirs.locations(), platform, t0);
  CHECK(store.mode() == Mode::Shared);
  CHECK(store.data_corrupt());
  CHECK(fs::exists(dirs.upstream() / "cache" / "cache.bad"));
  CHECK(store.string_or("nickname", "") == "n");
}

// --- leaving the Shared store ---

void conversion_copies_everything_and_leaves_upstream_alone() {
  TempDirs dirs;
  FakePlatform platform;
  write_upstream(dirs);
  const std::string before = read(dirs.upstream() / "settings.json");
  {
    auto store = Store::open(dirs.locations(), platform, t0);
    CHECK(store.convert_to_protected().has_value());
    CHECK(store.mode() == Mode::Protected);
    CHECK(!store.convert_to_protected().has_value());
  }
  CHECK(read(dirs.upstream() / "settings.json") == before);
  CHECK(read(dirs.mine() / "settings.json").find("tok") == std::string::npos);
  auto store = Store::open(dirs.locations(), platform, t0);
  CHECK(store.mode() == Mode::Protected);
  CHECK(store.string_or("token", "") == "tok");
  CHECK(store.string_or("nickname", "") == "구름");
  CHECK(store.resume()->key == "k1");
  CHECK(store.enter_room("ABC234").at(0).text == "hi");
}

void failed_conversion_stays_shared() {
  TempDirs dirs;
  FakePlatform platform;
  write_upstream(dirs);
  auto store = Store::open(dirs.locations(), platform, t0);
  platform.fail_write = true;
  CHECK(!store.convert_to_protected().has_value());
  CHECK(store.mode() == Mode::Shared);
  platform.fail_write = false;
  CHECK(Store::open(dirs.locations(), platform, t0).mode() == Mode::Shared);
  CHECK(store.convert_to_protected().has_value());
}

void erasing_removes_secrets_only() {
  TempDirs dirs;
  FakePlatform platform;
  write_upstream(dirs);
  write(dirs.upstream() / "session.json", R"({"resumeKey":"old"})");
  auto store = Store::open(dirs.locations(), platform, t0);
  CHECK(store.erase_shared_secrets().error() == Error::WrongMode);
  CHECK(store.convert_to_protected().has_value());
  CHECK(store.erase_shared_secrets().has_value());

  const auto settings = Json::parse(read(dirs.upstream() / "settings.json"));
  CHECK(settings->find("token")->is_null());
  CHECK(settings->find("gid")->is_null());
  CHECK(settings->find("nickname")->as_string() == "구름");
  CHECK(settings->find("futureKey") != nullptr);
  const std::string cache = mix(read(dirs.upstream() / "cache" / "cache"));
  CHECK(cache.find("k1") == std::string::npos);
  CHECK(cache.find("hi") == std::string::npos);
  CHECK(cache.find("futureTop") != std::string::npos);
  CHECK(!fs::exists(dirs.upstream() / "session.json"));
  CHECK(store.string_or("token", "") == "tok");
}

// --- Seats ---

void normalized_servers_compare_equal() {
  CHECK(normalize_server("https://cr.nanliburusu.com/") == "https://cr.nanliburusu.com");
  CHECK(normalize_server(" HTTPS://CR.Example.COM// ") == "https://cr.example.com");
  CHECK(normalize_server("cr.example.com") == "https://cr.example.com");
  CHECK(normalize_server("localhost:8008") == "http://localhost:8008");
  CHECK(normalize_server("https://a.com/Path/") == "https://a.com/Path");
}

void seats_expire_by_how_they_were_left() {
  TempDirs dirs;
  FakePlatform platform;
  auto store = Store::open(dirs.locations(), platform, t0);
  store.remember_seat("LEFT22", "k", true, true, t0);
  store.remember_seat("LOST22", "k", false, false, t0);
  CHECK(store.find_seat("LEFT22", t0 + left_grace_ms - 1).has_value());
  CHECK(!store.find_seat("LEFT22", t0 + left_grace_ms).has_value());
  CHECK(store.find_seat("LOST22", t0 + left_grace_ms).has_value());
  CHECK(!store.find_seat("LOST22", t0 + offline_grace_ms).has_value());
}

void a_newer_seat_replaces_the_old_one() {
  TempDirs dirs;
  FakePlatform platform;
  auto store = Store::open(dirs.locations(), platform, t0);
  store.remember_seat("ABC234", "old", false, true, t0);
  store.remember_seat("XYZ234", "other", false, true, t0 + 1);
  CHECK(store.latest_seat(t0 + 2)->code == "XYZ234");
  store.remember_seat("ABC234", "new", false, true, t0 + 3);
  CHECK(store.find_seat("ABC234", t0 + 3)->key == "new");
  CHECK(store.latest_seat(t0 + 3)->code == "ABC234");
  store.forget_seat("ABC234");
  CHECK(!store.find_seat("ABC234", t0 + 3));
  CHECK(store.latest_seat(t0 + 3)->code == "XYZ234");
}

void seats_belong_to_a_server_and_owner() {
  TempDirs dirs;
  FakePlatform platform;
  auto store = Store::open(dirs.locations(), platform, t0);
  store.remember_seat("ABC234", "k", false, true, t0);
  store.set("serverUrl", Json("https://other.example"));
  CHECK(!store.find_seat("ABC234", t0));
  CHECK(!store.latest_seat(t0));
  store.set("serverUrl", Json(std::string(default_server_url)));
  CHECK(store.find_seat("ABC234", t0).has_value());
  store.set("isGuest", Json(false));
  store.set("username", Json("me"));
  CHECK(!store.find_seat("ABC234", t0));
}

// Each server has its own accounts: switching keeps the one in use aside, secret, and brings back
// the one kept for the other server.
void each_server_keeps_its_own_account() {
  TempDirs dirs;
  FakePlatform platform;
  auto store = Store::open(dirs.locations(), platform, t0);
  store.set("token", Json("SECRET-main"));
  store.set("gid", Json("SECRET-gid"));
  store.set("nickname", Json("me"));
  store.set("isGuest", Json(false));
  store.set("volume", Json(30));
  store.set_resume({"ABC234", "SECRET-key", "", std::nullopt});
  store.switch_server("https://other.example/");
  CHECK(store.server() == "https://other.example");
  CHECK(store.string_or("token", "").empty());
  CHECK(store.string_or("nickname", "").empty());
  CHECK(store.string_or("gid", "").empty());
  CHECK(store.i64_or("volume", 0) == 30);  // settings are not the account's
  CHECK(!store.resume());
  store.set("nickname", Json("there"));
  CHECK(store.save().has_value());
  for (const auto& entry : fs::recursive_directory_iterator(dirs.mine())) {
    CHECK(read(entry.path()).find("SECRET") == std::string::npos);
  }

  auto again = Store::open(dirs.locations(), platform, t0);
  CHECK(again.server() == "https://other.example");
  again.switch_server("");
  CHECK(again.find("serverUrl") == nullptr || again.find("serverUrl")->is_null());
  CHECK(again.string_or("token", "") == "SECRET-main");
  CHECK(again.string_or("gid", "") == "SECRET-gid");
  CHECK(again.string_or("nickname", "") == "me");
  CHECK(!again.bool_or("isGuest", true));
  again.switch_server("https://OTHER.example");
  CHECK(again.string_or("nickname", "") == "there");
  CHECK(again.string_or("token", "").empty());
  // The same server again changes nothing.
  again.switch_server("https://other.example/");
  CHECK(again.string_or("nickname", "") == "there");
}

void bad_seat_entries_are_ignored() {
  TempDirs dirs;
  FakePlatform platform;
  write(dirs.upstream() / "settings.json", R"({})");
  write(dirs.upstream() / "cache" / "cache",
        mix(R"({"session":{"seatReturns":[5,{"code":"bad","key":"k","expiresAt":9e15},{"server":"https://cr.nanliburusu.com","owner":"guest","code":"ABC234","key":"k","public":true,"leftAt":1,"expiresAt":9000000000000000}]}})"));
  const auto store = Store::open(dirs.locations(), platform, t0);
  CHECK(store.latest_seat(t0)->code == "ABC234");
  CHECK(store.latest_seat(t0)->is_public);
}

void resume_ignores_a_ticket_for_another_room() {
  TempDirs dirs;
  FakePlatform platform;
  auto store = Store::open(dirs.locations(), platform, t0);
  CHECK(!store.resume());
  store.set_resume({"ABC234", "k", "t", std::nullopt});
  CHECK(store.resume()->ticket == "t");
  store.set("resumeCode", Json("XYZ234"));
  CHECK(store.resume()->ticket.empty());
  store.set_resume({"ABC234", "k", "", t0});
  CHECK(store.resume()->left_at == t0);
  store.clear_resume();
  CHECK(!store.resume());
}

// --- chat cache ---

void chat_is_bounded() {
  TempDirs dirs;
  FakePlatform platform;
  auto store = Store::open(dirs.locations(), platform, t0);
  for (std::size_t i = 0; i < chat_log_max + 5; ++i) {
    store.record_chat("ABC234", {0, "c", "n", "", std::to_string(i), static_cast<std::int64_t>(i)});
  }
  store.record_chat("ABC234", {0, "c", "n", "", "", 1});  // empty text is not kept
  store.record_chat("", {0, "c", "n", "", "x", 1});
  const auto log = store.enter_room("ABC234");
  CHECK(log.size() == chat_log_max);
  CHECK(log.front().text == "5");
  CHECK(log.back().text == std::to_string(chat_log_max + 4));
}

void chat_expires_after_leaving() {
  TempDirs dirs;
  FakePlatform platform;
  {
    auto store = Store::open(dirs.locations(), platform, t0);
    store.record_chat("LEFT22", {0, "c", "n", "", "a", 1});
    store.mark_left("LEFT22", t0);
    store.record_chat("OPEN22", {0, "c", "n", "", "b", 1});
    (void)store.enter_room("OPEN22");
    CHECK(store.save().has_value());
  }
  {
    // A Room still open when the app stopped counts as left at the next start.
    auto store = Store::open(dirs.locations(), platform, t0 + chat_ttl_ms);
    CHECK(store.enter_room("LEFT22").size() == 1);
    store.mark_left("LEFT22", t0);
    store.mark_left("OPEN22", t0 + chat_ttl_ms);
    CHECK(store.save().has_value());
  }
  auto store = Store::open(dirs.locations(), platform, t0 + chat_ttl_ms + 1);
  CHECK(store.enter_room("LEFT22").empty());
  CHECK(store.enter_room("OPEN22").size() == 1);
}

void chat_fields_round_trip() {
  TempDirs dirs;
  FakePlatform platform;
  auto store = Store::open(dirs.locations(), platform, t0);
  store.record_chat("ABC234", {std::nullopt, "c", "닉", "", "안녕\n\"x\"", 42});
  store.record_chat("ABC234", {3, "d", "n", "1f600", "b", 43});
  const auto log = store.enter_room("ABC234");
  CHECK(!log[0].slot);
  CHECK(log[0].nick == "닉" && log[0].text == "안녕\n\"x\"" && log[0].emoji.empty() && log[0].time_ms == 42);
  CHECK(log[1].slot == 3u && log[1].emoji == "1f600");
}

}  // namespace

void signing_out_forgets_chat_and_seats() {
  TempDirs dirs;
  FakePlatform platform;
  {
    auto store = Store::open(dirs.locations(), platform, t0);
    (void)store.enter_room("ABC234");
    store.record_chat("ABC234", {std::nullopt, "c", "n", "", "hello", t0});
    store.remember_seat("ABC234", "key", false, true, t0);
    CHECK(store.find_seat("ABC234", t0).has_value());
    CHECK(store.save().has_value());
    store.clear_chat();
    store.forget_all_seats();
    CHECK(store.has_unsaved());
    CHECK(store.save().has_value());
    CHECK(!store.find_seat("ABC234", t0).has_value());
  }
  auto store = Store::open(dirs.locations(), platform, t0);
  CHECK(!store.find_seat("ABC234", t0).has_value());
  CHECK(store.enter_room("ABC234").empty());
}

int main() {
  signing_out_forgets_chat_and_seats();
  new_user_gets_a_protected_store();
  existing_upstream_files_select_the_shared_store();
  protected_store_wins_when_upstream_arrives_later();
  secrets_never_reach_a_plain_file();
  unreadable_secrets_count_as_absent();
  tampered_secrets_count_as_absent();
  corrupt_settings_are_kept_aside();
  failed_saves_stay_pending();
  unchanged_values_write_nothing();
  failed_first_save_leaves_no_store();
  shared_files_keep_upstream_format_and_unknown_values();
  cache_encoding_matches_upstream();
  corrupt_cache_is_kept_aside();
  conversion_copies_everything_and_leaves_upstream_alone();
  failed_conversion_stays_shared();
  erasing_removes_secrets_only();
  normalized_servers_compare_equal();
  seats_expire_by_how_they_were_left();
  a_newer_seat_replaces_the_old_one();
  seats_belong_to_a_server_and_owner();
  each_server_keeps_its_own_account();
  bad_seat_entries_are_ignored();
  resume_ignores_a_ticket_for_another_room();
  chat_is_bounded();
  chat_expires_after_leaving();
  chat_fields_round_trip();
  return crpp_test_result();
}
