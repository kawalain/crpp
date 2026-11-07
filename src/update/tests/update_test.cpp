// The whole flow against a scripted platform: no network, no files, no real exe.
// `verify` here only compares bytes; real signatures are in windows_platform_test.
#include "crpp/update/update.hpp"

#include <map>
#include <set>

#include "check.hpp"

using namespace crpp::update;

namespace {

constexpr PublicKey test_key{1, 2, 3};
const Source source{"https://test/releases", "https://test/dl/"};
const std::string hash_of_exe(64, 'a');

std::vector<std::uint8_t> bytes_of(std::string_view s) { return {s.begin(), s.end()}; }

std::string manifest_text(std::string_view version = "0.2.0", std::string_view size = "3") {
  return "crpp-update 1\nversion " + std::string(version) + "\nasset crpp.exe\nsize " + std::string(size) + "\nsha256 " +
         hash_of_exe + "\n";
}
std::string signed_manifest(std::string_view version = "0.2.0") {
  return manifest_text(version) + "signature " + std::string(128, '0') + "\n";
}

class FakePlatform final : public Platform {
 public:
  std::map<std::string, std::string> web;
  std::vector<std::string> requested, log;
  std::map<std::filesystem::path, std::vector<std::uint8_t>> files;
  bool signature_good = true;
  bool launch_works = true, write_works = true, corrupt_write = false;
  std::set<std::string> failing_moves;  // "from>to" by file name

  std::expected<std::vector<std::uint8_t>, Error> get(const std::string& url, std::size_t limit, std::string_view,
                                                      const OnBytes& on_bytes) override {
    requested.push_back(url);
    const auto it = web.find(url);
    if (it == web.end()) return std::unexpected(Error::Unavailable);
    auto bytes = bytes_of(it->second);
    if (bytes.size() > limit + 1) bytes.resize(limit + 1);
    if (on_bytes) on_bytes(bytes.size());
    return bytes;
  }
  std::string sha256(std::span<const std::uint8_t> bytes) override {
    return bytes.size() == 3 && bytes[0] == 'n' ? hash_of_exe : std::string(64, 'b');
  }
  bool verify(const PublicKey& key, std::span<const std::uint8_t> message, const Signature&) override {
    verified_ = std::string(message.begin(), message.end());
    return signature_good && key == test_key;
  }
  bool write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) override {
    log.push_back("write " + path.filename().string());
    if (!write_works) return false;
    files[path] = corrupt_write ? bytes_of("xxx") : std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    return true;
  }
  std::expected<std::vector<std::uint8_t>, Error> read_file(const std::filesystem::path& path, std::size_t) override {
    const auto it = files.find(path);
    if (it == files.end()) return std::unexpected(Error::Disk);
    return it->second;
  }
  bool move_file(const std::filesystem::path& from, const std::filesystem::path& to) override {
    const std::string name = from.filename().string() + ">" + to.filename().string();
    log.push_back("move " + name);
    if (failing_moves.contains(name) || !files.contains(from)) return false;
    files[to] = std::move(files[from]);
    files.erase(from);
    return true;
  }
  bool remove_file(const std::filesystem::path& path) override {
    files.erase(path);
    return true;
  }
  bool launch(const std::filesystem::path& exe) override {
    log.push_back("launch " + exe.filename().string());
    return launch_works;
  }
  [[nodiscard]] const std::string& verified() const { return verified_; }

 private:
  std::string verified_;
};

constexpr std::string_view releases_json = R"([
  {"tag_name":"v0.2.0","name":"둘","body":"## 변경","published_at":"2026-11-04T09:00:00Z","draft":false,"prerelease":false,
   "assets":[{"name":"crpp.exe","size":3}],"html_url":"https://x"},
  {"tag_name":"v0.1.0","name":"하나","body":null,"published_at":"2026-10-01T00:00:00Z","draft":false,"prerelease":false},
  {"tag_name":"v9.0.0","draft":true},
  {"tag_name":"v8.0.0","prerelease":true},
  {"tag_name":"nightly"},
  {"tag_name":"0.9.0"}
])";

FakePlatform web_with_update() {
  FakePlatform p;
  p.web[source.releases_url] = std::string(releases_json);
  p.web["https://test/dl/v0.2.0/crpp-update.manifest"] = signed_manifest();
  p.web["https://test/dl/v0.2.0/crpp.exe"] = "new";
  return p;
}

void orders_versions() {
  CHECK(parse_version("0.1.10") > parse_version("0.1.9"));
  CHECK(parse_version("v0.2.0") > parse_version("0.1.99"));
  CHECK(parse_version("1.0.0") > parse_version("0.99.99"));
  CHECK(parse_version("v0.1.0") == parse_version("0.1.0"));
  CHECK(to_string(*parse_version("v0.12.3")) == "0.12.3");
}

void refuses_what_is_not_a_plain_version() {
  for (const char* bad : {"", "v", "1", "1.2", "1.2.3.4", "1.2.x", "01.2.3", "1.02.3", "1.2.3-rc1", " 1.2.3", "1.2.3 ",
                          "-1.2.3", "vv1.2.3", "99999999999.0.0", "1..3"}) {
    CHECK(!parse_version(bad).has_value());
  }
}

void parses_a_public_key() {
  CHECK(parse_public_key(std::string(128, 'a')).has_value());
  CHECK(!parse_public_key(std::string(128, 'A')).has_value());
  CHECK(!parse_public_key(std::string(126, 'a')).has_value());
  CHECK(!parse_public_key("").has_value());
  CHECK(!parse_public_key("PLACEHOLDER").has_value());
}

void parses_a_manifest() {
  const auto text = signed_manifest();
  const auto manifest = parse_manifest(bytes_of(text));
  CHECK(manifest.has_value());
  CHECK(manifest->version == (Version{0, 2, 0}));
  CHECK(manifest->asset == "crpp.exe" && manifest->size == 3 && manifest->sha256 == hash_of_exe);
  CHECK(manifest->signed_bytes == manifest_text().size());
}

void refuses_a_manifest_that_is_not_exactly_right() {
  const std::string sig = "signature " + std::string(128, '0') + "\n";
  const std::string good = manifest_text();
  std::vector<std::string> bad{
      "",
      good,                                           // no signature
      good + "signature abc\n",                       // short
      good + "signature " + std::string(128, 'A') + "\n",  // uppercase
      good + sig + "extra\n",                         // something after what is signed
      manifest_text("v0.2.0") + sig,                  // tag instead of version
      manifest_text("0.2") + sig,
      manifest_text("0.2.0", "0") + sig,              // empty
      manifest_text("0.2.0", "-1") + sig,
      manifest_text("0.2.0", "99999999999") + sig,    // over the limit
      "crpp-update 2\n" + good.substr(good.find('\n') + 1) + sig,
      "crpp-update 1\r\nversion 0.2.0\r\n" + sig,     // line endings changed on the way
      std::string(2000, 'x'),
  };
  for (const auto& text : bad) CHECK(!parse_manifest(bytes_of(text)).has_value());

  // An asset name that would leave the release's folder.
  for (const char* name : {"..", "../x.exe", "a/b.exe", "a\\b.exe", ".hidden"}) {
    std::string text = good;
    text.replace(text.find("crpp.exe"), 8, name);
    CHECK(!parse_manifest(bytes_of(text + sig)).has_value());
  }
  std::string short_hash = good;
  short_hash.replace(short_hash.find(hash_of_exe), 64, "abc");
  CHECK(!parse_manifest(bytes_of(short_hash + sig)).has_value());
}

void reads_the_list_of_releases() {
  const auto releases = parse_releases(releases_json);
  CHECK(releases.has_value());
  CHECK(releases->size() == 2);  // drafts, prereleases and odd tags left out
  CHECK((*releases)[0].tag() == "v0.2.0" && (*releases)[0].name == "둘" && (*releases)[0].body == "## 변경");
  CHECK((*releases)[1].version == (Version{0, 1, 0}) && (*releases)[1].body.empty());
  CHECK(!parse_releases("{").has_value());
  CHECK(!parse_releases(R"({"message":"rate limited"})").has_value());
  CHECK(parse_releases("[]")->empty());
  CHECK(parse_releases("[1, null, \"x\", {}]")->empty());
}

void orders_releases_by_version_not_by_listing() {
  const auto releases = parse_releases(R"([{"tag_name":"v0.1.0"},{"tag_name":"v0.10.0"},{"tag_name":"v0.9.0"}])");
  CHECK((*releases)[0].version.minor == 10 && (*releases)[1].version.minor == 9);
}

void the_cache_reads_back_the_same() {
  const auto releases = *parse_releases(releases_json);
  const auto again = parse_releases(dump_releases(releases));
  CHECK(again.has_value() && again->size() == 2);
  CHECK((*again)[0].name == "둘" && (*again)[0].body == "## 변경" && (*again)[0].published_at == "2026-11-04T09:00:00Z");
  // Download counts and the like are gone, so an unchanged release writes nothing.
  CHECK(dump_releases(*again) == dump_releases(releases));
}

void writes_dates_like_upstream() {
  CHECK(korean_date("2026-11-04T09:00:00Z") == "2026년 11월 04일");
  CHECK(korean_date("").empty());
  CHECK(korean_date("soon").empty());
  CHECK(korean_date("20261104T090000").empty());
}

void without_a_key_nothing_goes_out() {
  FakePlatform p = web_with_update();
  const auto result = check(source, {0, 1, 0}, std::nullopt, p);
  CHECK(!result && result.error() == Error::NoKey);
  CHECK(p.requested.empty());
}

void offers_a_newer_release_once_it_is_proved() {
  FakePlatform p = web_with_update();
  const auto result = check(source, {0, 1, 0}, test_key, p);
  CHECK(result.has_value());
  CHECK(result->update.has_value() && !result->problem);
  CHECK(result->update->release.tag() == "v0.2.0");
  CHECK(result->releases.size() == 2);
  CHECK(p.verified() == manifest_text());  // the signature covers everything before its line
}

void says_nothing_when_nothing_is_newer() {
  for (const Version current : {Version{0, 2, 0}, Version{0, 3, 0}}) {
    FakePlatform p = web_with_update();
    const auto result = check(source, current, test_key, p);
    CHECK(result.has_value() && !result->update && !result->problem);
    CHECK(p.requested.size() == 1);  // no manifest fetched
    CHECK(result->releases.size() == 2);
  }
}

void a_bad_signature_is_no_update() {
  FakePlatform p = web_with_update();
  p.signature_good = false;
  const auto result = check(source, {0, 1, 0}, test_key, p);
  CHECK(result.has_value() && !result->update && result->problem == Error::BadSignature);
  CHECK(result->releases.size() == 2);  // the changelog can still use the list
}

void another_key_is_no_update() {
  FakePlatform p = web_with_update();
  const auto result = check(source, {0, 1, 0}, PublicKey{9}, p);
  CHECK(result.has_value() && !result->update && result->problem == Error::BadSignature);
}

void an_old_signed_manifest_cannot_pass_as_new() {
  FakePlatform p = web_with_update();
  p.web["https://test/dl/v0.2.0/crpp-update.manifest"] = signed_manifest("0.1.5");
  const auto result = check(source, {0, 1, 0}, test_key, p);
  CHECK(result.has_value() && !result->update && result->problem == Error::BadContent);
}

void a_missing_manifest_is_no_update() {
  FakePlatform p = web_with_update();
  p.web.erase("https://test/dl/v0.2.0/crpp-update.manifest");
  const auto result = check(source, {0, 1, 0}, test_key, p);
  CHECK(result.has_value() && !result->update && result->problem == Error::Unavailable);
}

void failures_of_the_list_are_reported() {
  FakePlatform none;
  CHECK(check(source, {0, 1, 0}, test_key, none).error() == Error::Unavailable);
  FakePlatform nonsense;
  nonsense.web[source.releases_url] = "<html>";
  CHECK(check(source, {0, 1, 0}, test_key, nonsense).error() == Error::BadContent);
}

Available available_from(FakePlatform& p) { return *check(source, {0, 1, 0}, test_key, p)->update; }

void downloads_only_what_the_manifest_names() {
  FakePlatform p = web_with_update();
  const Available available = available_from(p);
  std::uint64_t seen = 0;
  const auto good = download(source, available, p, [&](std::uint64_t received) { seen = received; });
  CHECK(good && *good == bytes_of("new"));
  CHECK(seen == 3);

  p.web["https://test/dl/v0.2.0/crpp.exe"] = "bad";  // right size, wrong hash
  CHECK(download(source, available, p, nullptr).error() == Error::BadContent);
  p.web["https://test/dl/v0.2.0/crpp.exe"] = "ne";  // short
  CHECK(download(source, available, p, nullptr).error() == Error::BadContent);
  p.web["https://test/dl/v0.2.0/crpp.exe"] = "newer than it said";
  CHECK(download(source, available, p, nullptr).error() == Error::BadContent);
  p.web.erase("https://test/dl/v0.2.0/crpp.exe");
  CHECK(download(source, available, p, nullptr).error() == Error::Unavailable);
}

const std::filesystem::path exe = "dir/crpp.exe", fresh = "dir/crpp.exe.new", old = "dir/crpp.exe.old";

// What must hold after any failure: the old exe where it was, nothing else beside it.
void check_untouched(const FakePlatform& p) {
  CHECK(p.files.contains(exe) && p.files.at(exe) == bytes_of("old"));
  CHECK(!p.files.contains(fresh));
  CHECK(!p.files.contains(old));
}

FakePlatform with_running_exe() {
  FakePlatform p;
  p.files[exe] = bytes_of("old");
  return p;
}

void replaces_and_starts_the_new_exe() {
  FakePlatform p = with_running_exe();
  p.files[old] = bytes_of("left by an earlier run");
  const auto result = self_replace(exe, bytes_of("new"), hash_of_exe, p);
  CHECK(result.has_value());
  CHECK(p.files.at(exe) == bytes_of("new"));
  CHECK(p.files.at(old) == bytes_of("old"));  // the new process removes it
  CHECK(!p.files.contains(fresh));
  const std::vector<std::string> order{"write crpp.exe.new", "move crpp.exe>crpp.exe.old", "move crpp.exe.new>crpp.exe",
                                       "launch crpp.exe"};
  CHECK(p.log == order);
  remove_leftovers(exe, p);
  CHECK(p.files.size() == 1);
}

void a_full_disk_changes_nothing() {
  FakePlatform p = with_running_exe();
  p.write_works = false;
  CHECK(self_replace(exe, bytes_of("new"), hash_of_exe, p).error() == Error::Disk);
  check_untouched(p);
}

void a_bad_write_is_caught_before_the_swap() {
  FakePlatform p = with_running_exe();
  p.corrupt_write = true;
  CHECK(self_replace(exe, bytes_of("new"), hash_of_exe, p).error() == Error::Disk);
  check_untouched(p);
  CHECK(p.log.size() == 1);  // never got as far as moving anything
}

void a_locked_exe_changes_nothing() {
  FakePlatform p = with_running_exe();
  p.failing_moves.insert("crpp.exe>crpp.exe.old");
  CHECK(self_replace(exe, bytes_of("new"), hash_of_exe, p).error() == Error::Disk);
  check_untouched(p);
}

void a_failed_swap_is_rolled_back() {
  FakePlatform p = with_running_exe();
  p.failing_moves.insert("crpp.exe.new>crpp.exe");
  CHECK(self_replace(exe, bytes_of("new"), hash_of_exe, p).error() == Error::Disk);
  check_untouched(p);
}

void a_new_exe_that_does_not_start_is_rolled_back() {
  FakePlatform p = with_running_exe();
  p.launch_works = false;
  CHECK(self_replace(exe, bytes_of("new"), hash_of_exe, p).error() == Error::Launch);
  check_untouched(p);
}

void a_failed_rollback_says_so_and_keeps_the_old_exe() {
  FakePlatform swap = with_running_exe();
  swap.failing_moves = {"crpp.exe.new>crpp.exe", "crpp.exe.old>crpp.exe"};
  CHECK(self_replace(exe, bytes_of("new"), hash_of_exe, swap).error() == Error::RollbackFailed);
  CHECK(swap.files.at(old) == bytes_of("old"));

  FakePlatform start = with_running_exe();
  start.launch_works = false;
  start.failing_moves = {"crpp.exe.old>crpp.exe"};
  CHECK(self_replace(exe, bytes_of("new"), hash_of_exe, start).error() == Error::RollbackFailed);
  CHECK(start.files.at(old) == bytes_of("old"));
}

void notes_open_once_for_each_version() {
  CHECK(notes_due_when_noticed("", "0.2.0"));
  CHECK(!notes_due_when_noticed("0.2.0", "0.2.0"));  // the next check, or the next start
  CHECK(notes_due_when_noticed("0.2.0", "0.3.0"));

  CHECK(!notes_due_at_start("", "", "0.2.0"));  // a first run ever was updated from nothing
  CHECK(!notes_due_at_start("0.2.0", "", "0.2.0"));  // the same version again
  CHECK(!notes_due_at_start("0.1.0", "0.2.0", "0.2.0"));  // read before pressing update
  CHECK(notes_due_at_start("0.1.0", "", "0.2.0"));  // never read: noticed under the prompt
  CHECK(notes_due_at_start("0.1.0", "0.3.0", "0.2.0"));
}

}  // namespace

int main() {
  orders_versions();
  refuses_what_is_not_a_plain_version();
  parses_a_public_key();
  parses_a_manifest();
  refuses_a_manifest_that_is_not_exactly_right();
  reads_the_list_of_releases();
  orders_releases_by_version_not_by_listing();
  the_cache_reads_back_the_same();
  writes_dates_like_upstream();
  without_a_key_nothing_goes_out();
  offers_a_newer_release_once_it_is_proved();
  says_nothing_when_nothing_is_newer();
  a_bad_signature_is_no_update();
  another_key_is_no_update();
  an_old_signed_manifest_cannot_pass_as_new();
  a_missing_manifest_is_no_update();
  failures_of_the_list_are_reported();
  downloads_only_what_the_manifest_names();
  replaces_and_starts_the_new_exe();
  a_full_disk_changes_nothing();
  a_bad_write_is_caught_before_the_swap();
  a_locked_exe_changes_nothing();
  a_failed_swap_is_rolled_back();
  a_new_exe_that_does_not_start_is_rolled_back();
  a_failed_rollback_says_so_and_keeps_the_old_exe();
  notes_open_once_for_each_version();
  return crpp_test_result();
}
