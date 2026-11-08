#include <atomic>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <random>

#include "check.hpp"
#include "crpp/assets/assets.hpp"

using namespace crpp::assets;
namespace fs = std::filesystem;

namespace {

constexpr std::string_view base = "https://repo/";
constexpr std::string_view mirror_url = "https://mirror/";
constexpr crpp::update::PublicKey good_key{1};
constexpr crpp::update::Signature good_signature{7};

// The "hash" is the content itself, which keeps the expectations readable. A signature is
// good when it is good_signature under good_key, whatever it covers.
class Fake final : public Platform {
 public:
  std::expected<std::vector<std::uint8_t>, Error> fetch(const std::string& url, std::uint64_t limit,
                                                        const OnBytes& on_bytes) override {
    const std::lock_guard lock(mutex);
    asked.push_back(url);
    for (const std::string& prefix : down) {
      if (url.starts_with(prefix)) return std::unexpected(Error::Network);
    }
    if (auto it = passing.find(url); it != passing.end() && it->second > 0) {
      --it->second;
      return std::unexpected(Error::Network);
    }
    const auto it = served.find(url);
    if (it == served.end()) return std::unexpected(Error::Unavailable);
    const std::string body = it->second.substr(0, static_cast<std::size_t>(limit) + 1);
    if (on_bytes) on_bytes(body.size() / 2);
    return std::vector<std::uint8_t>(body.begin(), body.end());
  }
  std::string sha256(std::span<const std::uint8_t> bytes) override { return {bytes.begin(), bytes.end()}; }
  bool verify(const crpp::update::PublicKey& key, std::span<const std::uint8_t> message,
              const crpp::update::Signature& signature) override {
    return key == good_key && !message.empty() && signature == good_signature;
  }
  void wait(std::chrono::milliseconds) override { ++waits; }

  [[nodiscard]] std::size_t count(std::string_view prefix) {
    const std::lock_guard lock(mutex);
    return static_cast<std::size_t>(std::ranges::count_if(asked, [&](const std::string& url) { return url.starts_with(prefix); }));
  }

  std::map<std::string, std::string> served;
  std::map<std::string, int> passing;  // how many more times the network fails it
  std::vector<std::string> down;       // prefixes that never answer
  std::vector<std::string> asked;
  std::atomic<int> waits{0};
  std::mutex mutex;
};

class TempDir {
 public:
  TempDir() {
    std::random_device random;
    path = fs::temp_directory_path() / ("crpp_assets_test_" + std::to_string(random()));
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  fs::path path;
};

std::string read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

File file(std::string group, std::string path, std::string body, std::string source = "") {
  File f{.group = std::move(group), .path = std::move(path), .size = body.size(), .sha256 = std::move(body)};
  if (!source.empty()) {
    f.source = std::move(source);
    f.remote = "x/" + f.path;
  }
  return f;
}

Catalog two_skins() {
  Catalog catalog;
  catalog.key = good_key;
  catalog.sources = {{"gh", "https://gh/"}};
  catalog.skins = {{"cloud", 1, ""}, {"retro", 1, "cloud"}, {"plain", 1, ""}};
  catalog.default_skin = "cloud";
  catalog.files = {file("shared", "emoji/a.svg", "aaa", "gh"), file("skin:cloud", "fonts/f.ttf", "ffff", "gh"),
                   file("skin:retro", "skins/retro/pack.cskin", "rr", "gh"), file("skin:plain", "skins/plain/pack.cskin", "pp", "gh")};
  return catalog;
}

void serve(Fake& fake, const Catalog& catalog) {
  for (const File& f : catalog.files) fake.served[std::string(base) + f.path] = f.sha256;
}

const std::vector<std::string> shared_and_cloud{"shared", "skin:cloud"};

void installs_only_the_groups_asked_for() {
  TempDir dir;
  const Catalog catalog = two_skins();
  CHECK(missing(catalog, shared_and_cloud, dir.path).size() == 2);
  Fake fake;
  serve(fake, catalog);
  Progress last;
  CHECK(install(catalog, base, shared_and_cloud, dir.path, fake, [&](const Progress& p) {
    CHECK(p.bytes_done >= last.bytes_done && p.files_done >= last.files_done);
    last = p;
  }));
  CHECK(last.files_done == 2 && last.files == 2 && last.bytes_done == 7 && last.bytes == 7);
  CHECK(read(dir.path / "emoji/a.svg") == "aaa" && read(dir.path / "fonts/f.ttf") == "ffff");
  CHECK(!fs::exists(dir.path / "skins"));
  CHECK(missing(catalog, shared_and_cloud, dir.path).empty());

  // Nothing is fetched the second time.
  fake.asked.clear();
  CHECK(install(catalog, base, shared_and_cloud, dir.path, fake, nullptr));
  CHECK(fake.asked.empty());
}

void wants_what_is_needed_and_what_is_in_use() {
  TempDir dir;
  Catalog catalog = two_skins();
  catalog.files.push_back(file("sound:soft", "sounds/keyboard/soft/a.wav", "s"));
  CHECK((wanted_groups(catalog, dir.path) == std::vector<std::string>{"shared", "skin:cloud", "sound:soft"}));
  // A Skin installed once stays current, with what it extends.
  Fake fake;
  serve(fake, catalog);
  const std::vector<std::string> retro{"skin:retro"};
  CHECK(install(catalog, base, retro, dir.path, fake, nullptr));
  catalog.default_skin = "plain";
  CHECK((wanted_groups(catalog, dir.path) == std::vector<std::string>{"shared", "skin:plain", "skin:retro", "skin:cloud", "sound:soft"}));
}

void a_new_catalog_fetches_only_what_changed() {
  TempDir dir;
  Catalog catalog = two_skins();
  Fake fake;
  serve(fake, catalog);
  const std::vector<std::string> all{"shared", "skin:cloud", "skin:retro"};
  CHECK(install(catalog, base, all, dir.path, fake, nullptr));

  catalog.files[1] = file("skin:cloud", "fonts/f.ttf", "FFFFF", "gh");
  catalog.files.erase(catalog.files.begin() + 2);  // retro's pack is no longer listed
  serve(fake, catalog);
  fake.asked.clear();
  CHECK(install(catalog, base, shared_and_cloud, dir.path, fake, nullptr));
  CHECK(fake.asked == std::vector<std::string>{"https://repo/fonts/f.ttf"});
  CHECK(read(dir.path / "fonts/f.ttf") == "FFFFF");
  CHECK(!fs::exists(dir.path / "skins/retro/pack.cskin"));
}

void a_group_not_asked_for_stays_listed_while_it_matches() {
  TempDir dir;
  Catalog catalog = two_skins();
  Fake fake;
  serve(fake, catalog);
  const std::vector<std::string> retro{"skin:retro"};
  CHECK(install(catalog, base, retro, dir.path, fake, nullptr));
  CHECK(install(catalog, base, shared_and_cloud, dir.path, fake, nullptr));
  CHECK(missing(catalog, retro, dir.path).empty());
}

// A newer catalog used for another pack, as when a Theme is picked: a pack installed before stays
// wanted, and what changed in it is missing until its turn comes.
void a_group_not_asked_for_stays_wanted_when_it_changes() {
  TempDir dir;
  Catalog catalog = two_skins();
  Fake fake;
  serve(fake, catalog);
  const std::vector<std::string> retro{"skin:retro"}, plain{"skin:plain"};
  CHECK(install(catalog, base, retro, dir.path, fake, nullptr));
  catalog.files[2] = file("skin:retro", "skins/retro/pack.cskin", "RR", "gh");
  CHECK(install(catalog, base, plain, dir.path, fake, nullptr));
  const auto wanted = wanted_groups(catalog, dir.path);
  CHECK(std::ranges::find(wanted, "skin:retro") != wanted.end());
  CHECK(missing(catalog, retro, dir.path).size() == 1);
  CHECK(read(dir.path / "skins/retro/pack.cskin") == "rr");  // the old one is used until then
}

void what_an_older_version_installed_is_kept() {
  TempDir dir;
  const Catalog catalog = two_skins();
  // Left by an older version, or a run that stopped before its list: only the hash decides.
  fs::create_directories(dir.path / "emoji");
  std::ofstream(dir.path / "emoji/a.svg", std::ios::binary) << "aaa";
  Fake fake;
  serve(fake, catalog);
  CHECK(install(catalog, base, shared_and_cloud, dir.path, fake, nullptr));
  CHECK(fake.asked == std::vector<std::string>{"https://repo/fonts/f.ttf"});
}

void what_does_not_match_is_never_kept() {
  const Catalog catalog = two_skins();
  for (const char* body : {"xxx", "aaaa"}) {  // the right size with the wrong hash, and too long
    TempDir dir;
    Fake fake;
    fake.served["https://repo/emoji/a.svg"] = body;
    fake.served["https://gh/x/emoji/a.svg"] = body;
    const std::vector<std::string> shared{"shared"};
    CHECK(install(catalog, base, shared, dir.path, fake, nullptr).error() == Error::BadContent);
    CHECK(!fs::exists(dir.path / "emoji/a.svg"));
    CHECK(missing(catalog, shared, dir.path).size() == 1);
  }
}

void a_file_the_repository_lacks_comes_from_its_source() {
  TempDir dir;
  const Catalog catalog = two_skins();
  Fake fake;
  fake.served["https://gh/x/emoji/a.svg"] = "aaa";
  fake.served["https://repo/fonts/f.ttf"] = "ffff";
  CHECK(install(catalog, base, shared_and_cloud, dir.path, fake, nullptr));
  CHECK(read(dir.path / "emoji/a.svg") == "aaa");
  // The Repository answered, so it is still asked first.
  CHECK(fake.count("https://repo/") == 2);
}

void a_passing_failure_is_tried_again() {
  TempDir dir;
  const Catalog catalog = two_skins();
  Fake fake;
  serve(fake, catalog);
  fake.passing["https://repo/emoji/a.svg"] = 2;
  const std::vector<std::string> shared{"shared"};
  CHECK(install(catalog, base, shared, dir.path, fake, nullptr));
  CHECK(fake.waits == 2);
  CHECK(fake.count("https://gh/") == 0);
}

void the_sources_take_over_when_the_repository_does_not_answer() {
  TempDir dir;
  Catalog catalog = two_skins();
  for (int i = 0; i < 50; ++i) catalog.files.push_back(file("shared", "many/" + std::to_string(i), "m", "gh"));
  Fake fake;
  fake.down = {std::string(base)};
  for (const File& f : catalog.files) fake.served["https://gh/x/" + f.path] = f.sha256;
  CHECK(install(catalog, base, shared_and_cloud, dir.path, fake, nullptr));
  CHECK(missing(catalog, shared_and_cloud, dir.path).empty());
  // Each worker may have met the Repository once, three times over, before it was known down.
  CHECK(fake.count("https://repo/") <= 8 * 3);
  CHECK(fake.count("https://gh/") == 52);
}

void an_empty_base_goes_straight_to_the_sources() {
  TempDir dir;
  const Catalog catalog = two_skins();
  Fake fake;
  for (const File& f : catalog.files) fake.served["https://gh/x/" + f.path] = f.sha256;
  CHECK(install(catalog, "", shared_and_cloud, dir.path, fake, nullptr));
  CHECK(fake.count("https://repo/") == 0);
}

// Set while the first file arrives: nothing more begins, and what arrived is listed.
void a_cancel_begins_no_more_files_and_keeps_what_arrived() {
  TempDir dir;
  const Catalog catalog = two_skins();
  Fake fake;
  serve(fake, catalog);
  std::atomic<bool> cancel{false};
  const std::vector<std::string> shared{"shared"};
  CHECK(install(catalog, base, shared, dir.path, fake, [&](const Progress& p) {
    if (p.files_done == 1) cancel = true;
  }, &cancel));
  CHECK(missing(catalog, shared, dir.path).empty());
  cancel = true;
  fake.asked.clear();
  (void)install(catalog, base, shared_and_cloud, dir.path, fake, nullptr, &cancel);
  CHECK(fake.asked.empty());
  CHECK(missing(catalog, shared_and_cloud, dir.path).size() == 1);
}

void a_failed_run_keeps_what_arrived() {
  TempDir dir;
  const Catalog catalog = two_skins();
  Fake fake;
  serve(fake, catalog);
  // At both places: the Repository is taken for down once the font fails there.
  for (const File& f : catalog.files) fake.served["https://gh/x/" + f.path] = f.sha256;
  fake.down = {"https://repo/fonts/", "https://gh/x/fonts/"};
  CHECK(install(catalog, base, shared_and_cloud, dir.path, fake, nullptr).error() == Error::Network);
  CHECK(missing(catalog, shared_and_cloud, dir.path).size() == 1);  // what arrived is listed already

  fake.down.clear();
  fake.asked.clear();
  CHECK(install(catalog, base, shared_and_cloud, dir.path, fake, nullptr));
  CHECK(fake.asked == std::vector<std::string>{"https://repo/fonts/f.ttf"});
}

void a_bad_file_does_not_stop_the_others() {
  TempDir dir;
  const Catalog catalog = two_skins();
  Fake fake;
  serve(fake, catalog);
  fake.served["https://repo/emoji/a.svg"] = "xxx";
  CHECK(install(catalog, base, shared_and_cloud, dir.path, fake, nullptr).error() == Error::BadContent);
  CHECK(read(dir.path / "fonts/f.ttf") == "ffff");
  CHECK(missing(catalog, shared_and_cloud, dir.path).size() == 1);
}

void no_answer_from_anywhere_stops_soon() {
  TempDir dir;
  Catalog catalog;
  for (int i = 0; i < 100; ++i) catalog.files.push_back(file("shared", "many/" + std::to_string(i), "x", "gh"));
  catalog.sources = {{"gh", "https://gh/"}};
  Fake fake;
  fake.down = {"https://"};
  const std::vector<std::string> shared{"shared"};
  CHECK(install(catalog, base, shared, dir.path, fake, nullptr).error() == Error::Network);
  CHECK(fake.asked.size() <= 8 * 3 * 2);  // each worker's first file, at both places
  CHECK(missing(catalog, shared, dir.path).size() == 100);
}

void a_damaged_file_is_fetched_again() {
  TempDir dir;
  const Catalog catalog = two_skins();
  Fake fake;
  serve(fake, catalog);
  CHECK(install(catalog, base, shared_and_cloud, dir.path, fake, nullptr));
  fs::remove(dir.path / "fonts/f.ttf");
  CHECK(missing(catalog, shared_and_cloud, dir.path).size() == 1);
  CHECK(install(catalog, base, shared_and_cloud, dir.path, fake, nullptr));
  CHECK(read(dir.path / "fonts/f.ttf") == "ffff");
}

void many_files_arrive_with_progress_in_order() {
  TempDir dir;
  Catalog catalog;
  std::uint64_t expected = 0;
  for (int i = 0; i < 100; ++i) {
    catalog.files.push_back(file("shared", "many/" + std::to_string(i) + ".bin", std::string(static_cast<std::size_t>(i) + 1, 'x')));
    expected += static_cast<std::uint64_t>(i) + 1;
  }
  Fake fake;
  serve(fake, catalog);
  Progress last;
  int calls = 0;
  const std::vector<std::string> shared{"shared"};
  CHECK(install(catalog, base, shared, dir.path, fake, [&](const Progress& p) {
    CHECK(p.bytes == expected && p.files == 100);
    CHECK(p.bytes_done >= last.bytes_done && p.bytes_done <= p.bytes && p.files_done >= last.files_done);
    last = p;
    ++calls;
  }));
  CHECK(last.bytes_done == expected && last.files_done == 100);
  CHECK(calls >= 100);
  CHECK(missing(catalog, shared, dir.path).empty());
}

// --- catalogs ---

std::string catalog_text(std::string_view signature_hex) {
  return "crpp-catalog 1\nkey 01" + std::string(126, '0') + "\nfile shared a.txt 3 " + std::string(64, 'b') + "\nsignature " +
         std::string(signature_hex) + "\n";
}
const std::string signed_good = catalog_text("07" + std::string(126, '0'));
const std::string signed_bad = catalog_text("08" + std::string(126, '0'));

Repository repository() { return {std::string(base), std::string(mirror_url), good_key}; }

void a_catalog_is_kept_only_when_signed() {
  Fake fake;
  fake.served["https://repo/catalog.txt"] = signed_good;
  const auto fetched = fetch_catalog(repository(), fake);
  CHECK(fetched && !fetched->from_mirror && fetched->text == signed_good && fetched->catalog.files.size() == 1);

  fake.served["https://repo/catalog.txt"] = signed_bad;
  CHECK(fetch_catalog(Repository{std::string(base), "", good_key}, fake).error() == Error::BadSignature);
  CHECK(fetch_catalog(Repository{std::string(base), "", {2}}, fake).error() == Error::BadSignature);  // another key
  fake.served["https://repo/catalog.txt"] = catalog_text("07");
  CHECK(fetch_catalog(Repository{std::string(base), "", good_key}, fake).error() == Error::BadContent);
  fake.served["https://repo/catalog.txt"] = signed_good.substr(0, signed_good.find("signature"));
  CHECK(fetch_catalog(Repository{std::string(base), "", good_key}, fake).error() == Error::BadSignature);  // unsigned
}

void the_mirror_has_the_catalog_when_the_repository_does_not() {
  Fake fake;
  fake.served["https://mirror/catalog.txt"] = signed_good;
  fake.down = {std::string(base)};
  const auto fetched = fetch_catalog(repository(), fake);
  CHECK(fetched && fetched->from_mirror);
  CHECK(fake.count("https://repo/") == 3);  // tried twice more first

  fake.down.clear();
  fake.served["https://repo/catalog.txt"] = signed_bad;  // a bad answer is no reason to stop either
  CHECK(fetch_catalog(repository(), fake).has_value());
  fake.served.erase("https://mirror/catalog.txt");
  CHECK(fetch_catalog(repository(), fake).error() == Error::BadSignature);  // the first place's error
}

void a_kept_catalog_is_checked_again() {
  TempDir dir;
  Fake fake;
  CHECK(!load_catalog(dir.path, good_key, fake));
  CHECK(save_catalog(dir.path, signed_good));
  CHECK(load_catalog(dir.path, good_key, fake).has_value());
  CHECK(!load_catalog(dir.path, {2}, fake));
  CHECK(save_catalog(dir.path, signed_bad));
  CHECK(!load_catalog(dir.path, good_key, fake));
  CHECK(load_local_catalog(dir.path).has_value());  // beside the exe, the signature is not asked for
}

void a_new_repository_is_trusted_with_the_key_it_states() {
  Fake fake;
  const std::string head = "crpp-catalog 1\nkey 01" + std::string(126, '0') + "\nskin aurora 1\n";
  const std::string signature = "\nsignature 07" + std::string(126, '0') + "\n";
  const std::string good = head + "file skin:aurora skins/aurora/pack.cskin 3 " + std::string(64, 'b') + signature;
  fake.served["https://other/catalog.txt"] = good;
  const auto fetched = fetch_new_catalog("https://other/", fake);
  CHECK(fetched && fetched->catalog.key == good_key && fetched->text == good);
  // Only packs, each in its own folder.
  fake.served["https://other/catalog.txt"] = head + "file skin:aurora fonts/f.ttf 3 " + std::string(64, 'b') + signature;
  CHECK(fetch_new_catalog("https://other/", fake).error() == Error::BadContent);
  fake.served["https://other/catalog.txt"] = signed_good;  // a shared file
  CHECK(fetch_new_catalog("https://other/", fake).error() == Error::BadContent);
  fake.served["https://other/catalog.txt"] = signed_bad;
  CHECK(fetch_new_catalog("https://other/", fake).error() == Error::BadSignature);
  CHECK(fetch_new_catalog("https://none/", fake).error() == Error::Unavailable);
  CHECK(fingerprint(good_key, fake) == std::string(good_key.begin(), good_key.end()));
}

}  // namespace

int main() {
  installs_only_the_groups_asked_for();
  wants_what_is_needed_and_what_is_in_use();
  a_new_catalog_fetches_only_what_changed();
  a_group_not_asked_for_stays_listed_while_it_matches();
  a_group_not_asked_for_stays_wanted_when_it_changes();
  what_an_older_version_installed_is_kept();
  what_does_not_match_is_never_kept();
  a_file_the_repository_lacks_comes_from_its_source();
  a_passing_failure_is_tried_again();
  the_sources_take_over_when_the_repository_does_not_answer();
  an_empty_base_goes_straight_to_the_sources();
  a_cancel_begins_no_more_files_and_keeps_what_arrived();
  a_failed_run_keeps_what_arrived();
  a_bad_file_does_not_stop_the_others();
  no_answer_from_anywhere_stops_soon();
  a_damaged_file_is_fetched_again();
  many_files_arrive_with_progress_in_order();
  a_catalog_is_kept_only_when_signed();
  the_mirror_has_the_catalog_when_the_repository_does_not();
  a_kept_catalog_is_checked_again();
  a_new_repository_is_trusted_with_the_key_it_states();
  return crpp_test_result();
}
