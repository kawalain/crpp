#include <algorithm>
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

// The "hash" is the content itself, which keeps the expectations readable. Windows does not tell
// names apart by case, so contents that stand for different files differ in more than that. A signature is
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

// Every Repository's files, under their hashes.
fs::path store(const TempDir& dir) { return dir.path / "files"; }

// What the list in `dir` says is at that path: its file in the store, or empty when it is not
// listed. The fake's hashes are not hex, so this reads the list itself.
std::string got(const TempDir& dir, std::string_view path) {
  std::ifstream in(dir.path / ".installed");
  std::string hash, listed;
  while (in >> hash >> listed) {
    if (listed == path) return read(store(dir) / hash);
  }
  return {};
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
  for (const File& f : catalog.files) fake.served[std::string(base) + f.sha256] = f.sha256;
}

const std::vector<std::string> shared_and_cloud{"shared", "skin:cloud"};

void installs_only_the_groups_asked_for() {
  TempDir dir;
  const Catalog catalog = two_skins();
  CHECK(missing(catalog, shared_and_cloud, dir.path, store(dir)).size() == 2);
  Fake fake;
  serve(fake, catalog);
  Progress last;
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, [&](const Progress& p) {
    CHECK(p.bytes_done >= last.bytes_done && p.files_done >= last.files_done);
    last = p;
  }));
  CHECK(last.files_done == 2 && last.files == 2 && last.bytes_done == 7 && last.bytes == 7);
  CHECK(got(dir, "emoji/a.svg") == "aaa" && got(dir, "fonts/f.ttf") == "ffff");
  CHECK(got(dir, "skins/retro/pack.cskin").empty());
  CHECK(missing(catalog, shared_and_cloud, dir.path, store(dir)).empty());

  // Nothing is fetched the second time.
  fake.asked.clear();
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, nullptr));
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
  CHECK(install(catalog, base, retro, dir.path, store(dir), fake, nullptr));
  catalog.default_skin = "plain";
  CHECK((wanted_groups(catalog, dir.path) == std::vector<std::string>{"shared", "skin:plain", "skin:retro", "skin:cloud", "sound:soft"}));
}

// A Repository the user added keeps current only what was picked from it.
void keeps_only_what_is_installed() {
  TempDir dir;
  Catalog catalog = two_skins();
  catalog.files.push_back(file("sound:soft", "sounds/keyboard/soft/a.wav", "s"));
  CHECK(kept_groups(catalog, dir.path).empty());
  Fake fake;
  serve(fake, catalog);
  const std::vector<std::string> retro{"skin:retro"};
  CHECK(install(catalog, base, retro, dir.path, store(dir), fake, nullptr));
  CHECK((kept_groups(catalog, dir.path) == std::vector<std::string>{"skin:retro", "skin:cloud"}));
}

// A font in use cannot be replaced: it waits for a start that has not opened it.
void a_held_file_waits_for_the_next_start() {
  TempDir dir;
  Catalog catalog = two_skins();
  Fake fake;
  serve(fake, catalog);
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, nullptr));
  catalog.files[0] = file("shared", "emoji/a.svg", "a2a", "gh");
  catalog.files[1] = file("skin:cloud", "fonts/f.ttf", "FFFFF", "gh");
  serve(fake, catalog);
  fake.asked.clear();
  const std::vector<std::string> held{"fonts/f.ttf"};
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, nullptr, nullptr, held));
  CHECK(fake.asked == std::vector<std::string>{"https://repo/a2a"});
  CHECK(got(dir, "fonts/f.ttf") == "ffff");
  const auto left = missing(catalog, shared_and_cloud, dir.path, store(dir));
  CHECK(left.size() == 1 && left[0]->path == "fonts/f.ttf");
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, nullptr));
  CHECK(got(dir, "fonts/f.ttf") == "FFFFF");
}

// A pack held back keeps its folder as it is, files the catalog no longer lists included.
void a_held_folder_stays_as_it_is() {
  TempDir dir;
  Catalog catalog = two_skins();
  catalog.files.push_back(file("skin:retro", "skins/retro/screens/home.cskin", "hh", "gh"));
  Fake fake;
  serve(fake, catalog);
  const std::vector<std::string> both{"shared", "skin:cloud", "skin:retro"};
  CHECK(install(catalog, base, both, dir.path, store(dir), fake, nullptr));
  catalog.files[2] = file("skin:retro", "skins/retro/pack.cskin", "r2", "gh");
  catalog.files.pop_back();
  catalog.files[0] = file("shared", "emoji/a.svg", "a2a", "gh");
  serve(fake, catalog);
  const std::vector<std::string> held{"skins/retro/"};
  CHECK(install(catalog, base, both, dir.path, store(dir), fake, nullptr, nullptr, held));
  CHECK(got(dir, "skins/retro/pack.cskin") == "rr" && got(dir, "skins/retro/screens/home.cskin") == "hh");
  CHECK(got(dir, "emoji/a.svg") == "a2a");
  // Let go, it follows the catalog.
  CHECK(install(catalog, base, both, dir.path, store(dir), fake, nullptr));
  CHECK(got(dir, "skins/retro/pack.cskin") == "r2" && got(dir, "skins/retro/screens/home.cskin").empty());
}

// A Skin of a format this exe does not read is never fetched: the one installed stays, files
// outside its folder included, while the rest follows the catalog.
void a_pack_in_an_unreadable_format_keeps_the_installed_one() {
  TempDir dir;
  Catalog old = two_skins();
  old.files.push_back(file("skin:retro", "skins/retro/screens/home.cskin", "hh", "gh"));
  Fake fake;
  serve(fake, old);
  const std::vector<std::string> all{"shared", "skin:cloud", "skin:retro", "skin:plain"};
  CHECK(install(old, base, all, dir.path, store(dir), fake, nullptr));

  Catalog latest = two_skins();
  latest.skins[0].format = 2;  // cloud
  latest.skins[1].format = 2;  // retro
  latest.files = {file("shared", "emoji/a.svg", "a2a", "gh"), file("skin:cloud", "fonts/f.ttf", "FFFFF", "gh"),
                  file("skin:retro", "skins/retro/pack.cskin", "r2", "gh"),
                  file("skin:retro", "skins/retro/screens/menu.cskin", "mm", "gh"),
                  file("skin:plain", "skins/plain/pack.cskin", "p2", "gh")};
  serve(fake, latest);
  fake.asked.clear();
  std::vector<std::string> held;
  hold_unreadable(held, latest, &old, 1, 1);
  CHECK(install(latest, base, wanted_groups(latest, dir.path), dir.path, store(dir), fake, nullptr, nullptr, held));
  CHECK((fake.asked == std::vector<std::string>{"https://repo/a2a", "https://repo/p2"} ||
         fake.asked == std::vector<std::string>{"https://repo/p2", "https://repo/a2a"}));
  CHECK(got(dir, "fonts/f.ttf") == "ffff");
  CHECK(got(dir, "skins/retro/pack.cskin") == "rr" && got(dir, "skins/retro/screens/home.cskin") == "hh");
  CHECK(got(dir, "skins/retro/screens/menu.cskin").empty());
  CHECK(got(dir, "skins/plain/pack.cskin") == "p2" && got(dir, "emoji/a.svg") == "a2a");

  // An exe that reads format 2 takes it.
  held.clear();
  hold_unreadable(held, latest, &old, 1, 2);
  CHECK(held.empty());
  CHECK(install(latest, base, wanted_groups(latest, dir.path), dir.path, store(dir), fake, nullptr, nullptr, held));
  CHECK(got(dir, "fonts/f.ttf") == "FFFFF" && got(dir, "skins/retro/pack.cskin") == "r2");
  CHECK(got(dir, "skins/retro/screens/home.cskin").empty());
}

// A pack removed goes with its folder, and is no longer kept current; the rest stays.
void a_removed_pack_is_no_longer_installed() {
  TempDir dir;
  Catalog catalog = two_skins();
  catalog.files.push_back(file("skin:retro", "skins/retro/screens/home.cskin", "hh", "gh"));
  Fake fake;
  serve(fake, catalog);
  const std::vector<std::string> all{"shared", "skin:cloud", "skin:retro", "skin:plain"};
  CHECK(install(catalog, base, all, dir.path, store(dir), fake, nullptr));
  // A file an older catalog put in its folder, and one of no group the catalog knows now.
  std::ofstream(dir.path / ".installed", std::ios::binary | std::ios::app) << "o skins/retro/old.cskin\nx other/x.txt\n";
  const std::vector<std::string> retro{"skin:retro"};
  CHECK(uninstall(catalog, retro, dir.path));
  CHECK(got(dir, "skins/retro/pack.cskin").empty() && got(dir, "skins/retro/screens/home.cskin").empty());
  CHECK(got(dir, "skins/plain/pack.cskin") == "pp" && got(dir, "fonts/f.ttf") == "ffff");
  CHECK(missing(catalog, retro, dir.path, store(dir)).size() == 2);
  CHECK(missing(catalog, all, dir.path, store(dir)).size() == 2);
  const std::string list = read(dir.path / ".installed");
  CHECK(list.find("skins/retro") == std::string::npos && list.find("other/x.txt") != std::string::npos);
  const auto wanted = wanted_groups(catalog, dir.path);
  CHECK(std::ranges::find(wanted, "skin:retro") == wanted.end());
}

void a_new_catalog_fetches_only_what_changed() {
  TempDir dir;
  Catalog catalog = two_skins();
  Fake fake;
  serve(fake, catalog);
  const std::vector<std::string> all{"shared", "skin:cloud", "skin:retro"};
  CHECK(install(catalog, base, all, dir.path, store(dir), fake, nullptr));

  catalog.files[1] = file("skin:cloud", "fonts/f.ttf", "FFFFF", "gh");
  catalog.files.erase(catalog.files.begin() + 2);  // retro's pack is no longer listed
  serve(fake, catalog);
  fake.asked.clear();
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, nullptr));
  CHECK(fake.asked == std::vector<std::string>{"https://repo/FFFFF"});
  CHECK(got(dir, "fonts/f.ttf") == "FFFFF");
  CHECK(got(dir, "skins/retro/pack.cskin").empty());
}

void a_group_not_asked_for_stays_listed_while_it_matches() {
  TempDir dir;
  Catalog catalog = two_skins();
  Fake fake;
  serve(fake, catalog);
  const std::vector<std::string> retro{"skin:retro"};
  CHECK(install(catalog, base, retro, dir.path, store(dir), fake, nullptr));
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, nullptr));
  CHECK(missing(catalog, retro, dir.path, store(dir)).empty());
}

// A newer catalog used for another pack, as when a Theme is picked: a pack installed before stays
// wanted, and what changed in it is missing until its turn comes.
void a_group_not_asked_for_stays_wanted_when_it_changes() {
  TempDir dir;
  Catalog catalog = two_skins();
  Fake fake;
  serve(fake, catalog);
  const std::vector<std::string> retro{"skin:retro"}, plain{"skin:plain"};
  CHECK(install(catalog, base, retro, dir.path, store(dir), fake, nullptr));
  catalog.files[2] = file("skin:retro", "skins/retro/pack.cskin", "r2", "gh");
  CHECK(install(catalog, base, plain, dir.path, store(dir), fake, nullptr));
  const auto wanted = wanted_groups(catalog, dir.path);
  CHECK(std::ranges::find(wanted, "skin:retro") != wanted.end());
  CHECK(missing(catalog, retro, dir.path, store(dir)).size() == 1);
  CHECK(got(dir, "skins/retro/pack.cskin") == "rr");  // the old one is used until then
}

void what_the_store_has_is_kept() {
  TempDir dir;
  const Catalog catalog = two_skins();
  // Another Repository's, or a run that stopped before its list: only the hash decides.
  fs::create_directories(store(dir));
  std::ofstream(store(dir) / "aaa", std::ios::binary) << "aaa";
  Fake fake;
  serve(fake, catalog);
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, nullptr));
  CHECK(fake.asked == std::vector<std::string>{"https://repo/ffff"});
}

// Two paths with the same bytes, as two packs that share a sound: one request, one file.
void the_same_content_is_fetched_once() {
  TempDir dir;
  Catalog catalog = two_skins();
  catalog.files.push_back(file("shared", "emoji/b.svg", "aaa"));
  Fake fake;
  serve(fake, catalog);
  Progress last;
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, [&](const Progress& p) { last = p; }));
  CHECK(fake.count("https://repo/aaa") == 1 && last.files == 2 && last.bytes == 7);
  CHECK(got(dir, "emoji/a.svg") == "aaa" && got(dir, "emoji/b.svg") == "aaa");
}

std::string hex(char c) { return std::string(64, c); }

void the_list_finds_each_file_in_the_store() {
  TempDir dir;
  fs::create_directories(dir.path);
  std::ofstream(dir.path / ".installed", std::ios::binary) << hex('a') << " skins/cloud/pack.cskin\n"
                                                           << hex('b') << " ../outside.txt\nnot-a-hash emoji/x.svg\n";
  const crpp::files::Tree tree = installed_tree(dir.path, store(dir));
  CHECK(tree.size() == 1 && *tree.find("skins/cloud/pack.cskin") == store(dir) / hex('a'));
}

void what_nothing_lists_leaves_the_store() {
  TempDir one, two;
  fs::create_directories(store(one));
  for (const char c : {'a', 'b', 'c'}) std::ofstream(store(one) / hex(c), std::ios::binary) << c;
  std::ofstream(store(one) / (hex('d') + ".part3"), std::ios::binary) << "half";
  fs::create_directories(two.path);
  std::ofstream(one.path / ".installed", std::ios::binary) << hex('a') << " fonts/f.ttf\n";
  std::ofstream(two.path / ".installed", std::ios::binary) << hex('b') << " skins/x/pack.cskin\n";
  const fs::path dirs[] = {one.path, two.path};
  collect_garbage(store(one), dirs);
  CHECK(fs::exists(store(one) / hex('a')) && fs::exists(store(one) / hex('b')));
  CHECK(!fs::exists(store(one) / hex('c')) && !fs::exists(store(one) / (hex('d') + ".part3")));
}

// As an exe before ADR 0010 left it: each file at its path, the list naming its hash.
void an_install_by_path_moves_into_the_store() {
  TempDir dir;
  const std::string font = hex('f'), wrong = hex('e');
  fs::create_directories(dir.path / "fonts");
  fs::create_directories(dir.path / "skins/cloud/screens");
  std::ofstream(dir.path / "fonts/f.ttf", std::ios::binary) << font;  // the fake hashes to the bytes
  std::ofstream(dir.path / "skins/cloud/screens/home.cskin", std::ios::binary) << "changed by hand";
  std::ofstream(dir.path / "catalog.txt", std::ios::binary) << "kept";
  std::ofstream(dir.path / ".installed", std::ios::binary) << font << " fonts/f.ttf\n" << wrong << " skins/cloud/screens/home.cskin\n";
  Fake fake;
  move_into_store(dir.path, store(dir), fake);
  CHECK(read(store(dir) / font) == font && !fs::exists(store(dir) / wrong));
  CHECK(!fs::exists(dir.path / "fonts") && !fs::exists(dir.path / "skins"));
  CHECK(fs::exists(dir.path / "catalog.txt") && fs::exists(dir.path / ".installed"));
  // Not what the list says, it is missing and comes again.
  Catalog catalog;
  catalog.files = {file("skin:cloud", "skins/cloud/screens/home.cskin", wrong), file("skin:cloud", "fonts/f.ttf", font)};
  const std::vector<std::string> cloud{"skin:cloud"};
  const auto left = missing(catalog, cloud, dir.path, store(dir));
  CHECK(left.size() == 1 && left[0]->path == "skins/cloud/screens/home.cskin");
  move_into_store(dir.path, store(dir), fake);  // nothing left to move
  CHECK(read(store(dir) / font) == font);
}

void what_does_not_match_is_never_kept() {
  const Catalog catalog = two_skins();
  for (const char* body : {"xxx", "aaaa"}) {  // the right size with the wrong hash, and too long
    TempDir dir;
    Fake fake;
    fake.served["https://repo/aaa"] = body;
    fake.served["https://gh/x/emoji/a.svg"] = body;
    const std::vector<std::string> shared{"shared"};
    CHECK(install(catalog, base, shared, dir.path, store(dir), fake, nullptr).error() == Error::BadContent);
    CHECK(!fs::exists(store(dir) / "aaa") && !fs::exists(store(dir) / body));
    CHECK(missing(catalog, shared, dir.path, store(dir)).size() == 1);
  }
}

void a_file_the_repository_lacks_comes_from_its_source() {
  TempDir dir;
  const Catalog catalog = two_skins();
  Fake fake;
  fake.served["https://gh/x/emoji/a.svg"] = "aaa";
  fake.served["https://repo/ffff"] = "ffff";
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, nullptr));
  CHECK(got(dir, "emoji/a.svg") == "aaa");
  // The Repository answered, so it is still asked first.
  CHECK(fake.count("https://repo/") == 2);
}

void a_passing_failure_is_tried_again() {
  TempDir dir;
  const Catalog catalog = two_skins();
  Fake fake;
  serve(fake, catalog);
  fake.passing["https://repo/aaa"] = 2;
  const std::vector<std::string> shared{"shared"};
  CHECK(install(catalog, base, shared, dir.path, store(dir), fake, nullptr));
  CHECK(fake.waits == 2);
  CHECK(fake.count("https://gh/") == 0);
}

void the_sources_take_over_when_the_repository_does_not_answer() {
  TempDir dir;
  Catalog catalog = two_skins();
  for (int i = 0; i < 50; ++i) catalog.files.push_back(file("shared", "many/" + std::to_string(i), "m" + std::to_string(i), "gh"));
  Fake fake;
  fake.down = {std::string(base)};
  for (const File& f : catalog.files) fake.served["https://gh/x/" + f.path] = f.sha256;
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, nullptr));
  CHECK(missing(catalog, shared_and_cloud, dir.path, store(dir)).empty());
  // Each worker may have met the Repository once, three times over, before it was known down.
  CHECK(fake.count("https://repo/") <= 8 * 3);
  CHECK(fake.count("https://gh/") == 52);
}

// The same files under the same hashes at the catalog's mirror, before a file's own source.
void the_catalogs_mirror_comes_before_the_sources() {
  TempDir dir;
  Catalog catalog = two_skins();
  catalog.mirror = "https://mirror/s/";
  Fake fake;
  fake.down = {std::string(base)};
  for (const File& f : catalog.files) fake.served["https://mirror/s/" + f.sha256] = f.sha256;
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, nullptr));
  CHECK(got(dir, "emoji/a.svg") == "aaa" && got(dir, "fonts/f.ttf") == "ffff");
  CHECK(fake.count("https://gh/") == 0);
  // And with the mirror down too, the sources.
  TempDir again;
  fake.down.push_back("https://mirror/");
  for (const File& f : catalog.files) fake.served["https://gh/x/" + f.path] = f.sha256;
  CHECK(install(catalog, base, shared_and_cloud, again.path, store(again), fake, nullptr));
  CHECK(fake.count("https://gh/") == 2);
}

void an_empty_base_goes_straight_to_the_sources() {
  TempDir dir;
  const Catalog catalog = two_skins();
  Fake fake;
  for (const File& f : catalog.files) fake.served["https://gh/x/" + f.path] = f.sha256;
  CHECK(install(catalog, "", shared_and_cloud, dir.path, store(dir), fake, nullptr));
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
  CHECK(install(catalog, base, shared, dir.path, store(dir), fake, [&](const Progress& p) {
    if (p.files_done == 1) cancel = true;
  }, &cancel));
  CHECK(missing(catalog, shared, dir.path, store(dir)).empty());
  cancel = true;
  fake.asked.clear();
  (void)install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, nullptr, &cancel);
  CHECK(fake.asked.empty());
  CHECK(missing(catalog, shared_and_cloud, dir.path, store(dir)).size() == 1);
}

void a_failed_run_keeps_what_arrived() {
  TempDir dir;
  const Catalog catalog = two_skins();
  Fake fake;
  serve(fake, catalog);
  // At both places: the Repository is taken for down once the font fails there.
  for (const File& f : catalog.files) fake.served["https://gh/x/" + f.path] = f.sha256;
  fake.down = {"https://repo/ffff", "https://gh/x/fonts/"};
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, nullptr).error() == Error::Network);
  CHECK(missing(catalog, shared_and_cloud, dir.path, store(dir)).size() == 1);  // what arrived is listed already

  fake.down.clear();
  fake.asked.clear();
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, nullptr));
  CHECK(fake.asked == std::vector<std::string>{"https://repo/ffff"});
}

void a_bad_file_does_not_stop_the_others() {
  TempDir dir;
  const Catalog catalog = two_skins();
  Fake fake;
  serve(fake, catalog);
  fake.served["https://repo/aaa"] = "xxx";
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, nullptr).error() == Error::BadContent);
  CHECK(got(dir, "fonts/f.ttf") == "ffff");
  CHECK(missing(catalog, shared_and_cloud, dir.path, store(dir)).size() == 1);
}

void no_answer_from_anywhere_stops_soon() {
  TempDir dir;
  Catalog catalog;
  for (int i = 0; i < 100; ++i) catalog.files.push_back(file("shared", "many/" + std::to_string(i), "x", "gh"));
  catalog.sources = {{"gh", "https://gh/"}};
  Fake fake;
  fake.down = {"https://"};
  const std::vector<std::string> shared{"shared"};
  CHECK(install(catalog, base, shared, dir.path, store(dir), fake, nullptr).error() == Error::Network);
  CHECK(fake.asked.size() <= 8 * 3 * 2);  // each worker's first file, at both places
  CHECK(missing(catalog, shared, dir.path, store(dir)).size() == 100);
}

void a_damaged_file_is_fetched_again() {
  TempDir dir;
  const Catalog catalog = two_skins();
  Fake fake;
  serve(fake, catalog);
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, nullptr));
  fs::remove(store(dir) / "ffff");
  CHECK(missing(catalog, shared_and_cloud, dir.path, store(dir)).size() == 1);
  CHECK(install(catalog, base, shared_and_cloud, dir.path, store(dir), fake, nullptr));
  CHECK(got(dir, "fonts/f.ttf") == "ffff");
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
  CHECK(install(catalog, base, shared, dir.path, store(dir), fake, [&](const Progress& p) {
    CHECK(p.bytes == expected && p.files == 100);
    CHECK(p.bytes_done >= last.bytes_done && p.bytes_done <= p.bytes && p.files_done >= last.files_done);
    last = p;
    ++calls;
  }));
  CHECK(last.bytes_done == expected && last.files_done == 100);
  CHECK(calls >= 100);
  CHECK(missing(catalog, shared, dir.path, store(dir)).empty());
}

// --- catalogs ---

std::string catalog_text(std::string_view signature_hex) {
  return "crpp-catalog 2\nkey 01" + std::string(126, '0') + "\nfile shared a.txt 3 " + std::string(64, 'b') + "\nsignature " +
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

// A Repository must serve the current grammar; one an older exe kept is still read from disk.
void a_catalog_of_the_old_grammar_is_only_read_from_disk() {
  std::string old = signed_good;
  old.replace(0, std::string_view("crpp-catalog 2").size(), "crpp-catalog 1");
  Fake fake;
  fake.served["https://repo/catalog.txt"] = old;
  CHECK(fetch_catalog(Repository{std::string(base), "", good_key}, fake).error() == Error::Outdated);
  fake.served["https://other/catalog.txt"] = old;
  CHECK(fetch_new_catalog("https://other/", fake).error() == Error::Outdated);
  TempDir dir;
  CHECK(save_catalog(dir.path, old));
  const auto kept = load_catalog(dir.path, good_key, fake);
  CHECK(kept && kept->grammar == 1);
}

void a_catalog_has_the_name_its_repository_gives() {
  Fake fake;
  fake.served["https://repo/catalog-2.txt"] = signed_good;
  Repository official{std::string(base), "", good_key};
  official.catalog = "catalog-2.txt";
  CHECK(fetch_catalog(official, fake).has_value());
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
  const std::string head = "crpp-catalog 2\nkey 01" + std::string(126, '0') + "\nskin aurora 1\n";
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

// A chain crosses Repositories by fingerprint, or reaches the official one, and stops where a
// Repository is not added.
void a_chain_crosses_repositories() {
  Catalog official, mine, theirs;
  official.skins = {{.id = "retro", .format = 1}, {.id = "cloud", .format = 1}};
  official.skins[0].version = "0.3.1";
  official.files = {{.group = "skin:retro", .path = "skins/retro/pack.cskin", .size = 5, .sha256 = "r"}};
  theirs.skins = {{.id = "dusk", .format = 1}};
  theirs.skins[0].pin = ParentPin{.id = "retro", .version = "0.3"};
  mine.skins = {{.id = "aurora", .format = 1, .parent = "base"}, {.id = "base", .format = 1}, {.id = "far", .format = 1}};
  mine.skins[1].pin = ParentPin{.id = "dusk", .fingerprint = "f-theirs"};
  mine.skins[2].pin = ParentPin{.id = "x", .fingerprint = "f-nobody", .repository = "github:nobody/skins"};
  const Shelf shelves[] = {{"f-official", &official}, {"f-mine", &mine}, {"f-theirs", &theirs}};

  const Chain chain = pack_chain(shelves, 0, 1, "aurora");
  CHECK(chain.gap == Chain::Gap::None && chain.links.size() == 4);
  if (chain.links.size() == 4) {
    CHECK(chain.links[0].shelf == 1 && chain.links[0].pack == "aurora" && !chain.links[0].pin);
    CHECK(chain.links[1].shelf == 1 && chain.links[1].pack == "base" && !chain.links[1].pin);
    CHECK(chain.links[2].shelf == 2 && chain.links[2].pack == "dusk" && chain.links[2].pin == &*mine.skins[1].pin);
    CHECK(chain.links[3].shelf == 0 && chain.links[3].pack == "retro" && chain.links[3].pin->version == "0.3");
  }
  const Chain far = pack_chain(shelves, 0, 1, "far");
  CHECK(far.gap == Chain::Gap::Repository && far.wanted && far.wanted->repository == "github:nobody/skins" && far.links.size() == 1);
  CHECK(pack_chain(shelves, 0, 1, "none").gap == Chain::Gap::Missing);
  CHECK(pack_chain(shelves, 0, 0, "cloud").links.size() == 1);
  // With no official Repository, as beside a development exe, what it has is missing.
  CHECK(pack_chain(shelves, static_cast<std::size_t>(-1), 2, "dusk").gap == Chain::Gap::Missing);
  // One more level is too deep, and so is a chain that comes round.
  official.skins[0].pin = ParentPin{.id = "cloud"};
  official.skins[0].parent = "cloud";
  CHECK(pack_chain(shelves, 0, 1, "aurora").gap == Chain::Gap::TooDeep);
  theirs.skins[0].pin = ParentPin{.id = "aurora", .fingerprint = "f-mine"};
  CHECK(pack_chain(shelves, 0, 2, "dusk").gap == Chain::Gap::TooDeep);
}

void a_pin_holds_to_version_and_digest() {
  Fake platform;
  Catalog catalog;
  catalog.skins = {{.id = "retro", .format = 1}};
  catalog.skins[0].version = "0.3.1";
  catalog.files = {{.group = "skin:retro", .path = "skins/retro/pack.cskin", .size = 5, .sha256 = "r"}};
  const std::string digest = own_digest(catalog, "retro", platform);
  CHECK(digest == "skins/retro/pack.cskin 5 r\n");  // the fake's hash is the text
  CHECK(check_pin({.id = "retro"}, catalog, "retro", platform) == PinCheck::Holds);
  CHECK(check_pin({.id = "retro", .version = "0.3", .sha256 = digest}, catalog, "retro", platform) == PinCheck::Holds);
  CHECK(check_pin({.id = "retro", .version = "0.4"}, catalog, "retro", platform) == PinCheck::Version);
  CHECK(check_pin({.id = "retro", .version = "0.4", .sha256 = "other"}, catalog, "retro", platform) == PinCheck::Digest);
  catalog.skins[0].version.clear();
  CHECK(check_pin({.id = "retro", .version = "0.3"}, catalog, "retro", platform) == PinCheck::Version);
  CHECK(check_pin({.id = "retro", .version = "0.3"}, catalog, "none", platform) == PinCheck::Version);
}

}  // namespace

int main() {
  a_chain_crosses_repositories();
  a_pin_holds_to_version_and_digest();
  installs_only_the_groups_asked_for();
  wants_what_is_needed_and_what_is_in_use();
  a_new_catalog_fetches_only_what_changed();
  keeps_only_what_is_installed();
  a_held_file_waits_for_the_next_start();
  a_held_folder_stays_as_it_is();
  a_pack_in_an_unreadable_format_keeps_the_installed_one();
  a_removed_pack_is_no_longer_installed();
  a_group_not_asked_for_stays_listed_while_it_matches();
  a_group_not_asked_for_stays_wanted_when_it_changes();
  what_the_store_has_is_kept();
  the_same_content_is_fetched_once();
  the_list_finds_each_file_in_the_store();
  what_nothing_lists_leaves_the_store();
  an_install_by_path_moves_into_the_store();
  what_does_not_match_is_never_kept();
  a_file_the_repository_lacks_comes_from_its_source();
  a_passing_failure_is_tried_again();
  the_sources_take_over_when_the_repository_does_not_answer();
  the_catalogs_mirror_comes_before_the_sources();
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
  a_catalog_of_the_old_grammar_is_only_read_from_disk();
  a_catalog_has_the_name_its_repository_gives();
  a_new_repository_is_trusted_with_the_key_it_states();
  return crpp_test_result();
}
