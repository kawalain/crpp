#include "crpp/assets/assets.hpp"

#include <array>
#include <atomic>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <random>

#include "check.hpp"

using namespace crpp::assets;
namespace fs = std::filesystem;

namespace {

// The "hash" is the content itself, which keeps the expectations readable.
class Fake final : public Platform {
 public:
  std::expected<std::vector<std::uint8_t>, Error> fetch(const Entry& entry, std::string_view remote,
                                                        const OnBytes& on_bytes) override {
    const std::lock_guard lock(mutex);
    fetched.emplace_back(entry.path);
    remotes.emplace_back(remote);
    if (entry.path == fail_path) return std::unexpected(Error::Network);
    const std::string& body = served[std::string(entry.path)];
    if (on_bytes) on_bytes(body.size() / 2);
    return std::vector<std::uint8_t>(body.begin(), body.end());
  }
  std::string sha256(std::span<const std::uint8_t> bytes) override { return {bytes.begin(), bytes.end()}; }

  std::map<std::string, std::string> served;
  std::vector<std::string> fetched, remotes;
  std::string fail_path;
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

const Entry a{"fonts/a.bin", 3, "aaa"};
const Entry b{"sounds/pack/b.bin", 2, "bb"};

void serve_both(Fake& fake) {
  fake.served["fonts/a.bin"] = "aaa";
  fake.served["sounds/pack/b.bin"] = "bb";
}

void installs_everything_once() {
  TempDir dir;
  const std::array entries{a, b};
  const Manifest manifest{entries};
  CHECK(missing(manifest, dir.path).size() == 2);

  Fake fake;
  serve_both(fake);
  std::uint64_t last_done = 0, last_total = 0;
  CHECK(install(manifest, dir.path, fake, [&](std::uint64_t done, std::uint64_t total) {
    CHECK(done >= last_done);
    last_done = done;
    last_total = total;
  }));
  CHECK(last_done == 5 && last_total == 5);
  CHECK(read(dir.path / "fonts/a.bin") == "aaa");
  CHECK(read(dir.path / "sounds/pack/b.bin") == "bb");
  CHECK(missing(manifest, dir.path).empty());

  // Nothing is fetched the second time.
  fake.fetched.clear();
  CHECK(install(manifest, dir.path, fake, nullptr));
  CHECK(fake.fetched.empty());
}

void a_new_manifest_fetches_only_what_changed() {
  TempDir dir;
  const std::array entries{a, b};
  Fake fake;
  serve_both(fake);
  CHECK(install(Manifest{entries}, dir.path, fake, nullptr));

  const Entry changed{"fonts/a.bin", 4, "AAAA"};
  const std::array next{changed};
  const Manifest manifest{next};
  CHECK(missing(manifest, dir.path).size() == 1);
  fake.served["fonts/a.bin"] = "AAAA";
  fake.fetched.clear();
  CHECK(install(manifest, dir.path, fake, nullptr));
  CHECK(fake.fetched == std::vector<std::string>{"fonts/a.bin"});
  CHECK(read(dir.path / "fonts/a.bin") == "AAAA");
  // The file the new manifest no longer lists is gone.
  CHECK(!fs::exists(dir.path / "sounds/pack/b.bin"));
}

void what_does_not_match_is_never_kept() {
  const std::array entries{a};
  const Manifest manifest{entries};
  {
    TempDir dir;
    Fake fake;
    fake.served["fonts/a.bin"] = "xxx";  // the right size, the wrong hash
    CHECK(install(manifest, dir.path, fake, nullptr).error() == Error::BadContent);
    CHECK(!fs::exists(dir.path / "fonts/a.bin"));
    CHECK(missing(manifest, dir.path).size() == 1);
  }
  {
    TempDir dir;
    Fake fake;
    fake.served["fonts/a.bin"] = "aaaa";  // too long
    CHECK(install(manifest, dir.path, fake, nullptr).error() == Error::BadContent);
    CHECK(!fs::exists(dir.path / "fonts/a.bin"));
  }
}

void a_failed_run_keeps_what_arrived() {
  TempDir dir;
  const std::array entries{a, b};
  const Manifest manifest{entries};
  Fake fake;
  serve_both(fake);
  fake.fail_path = "sounds/pack/b.bin";
  CHECK(install(manifest, dir.path, fake, nullptr).error() == Error::Network);
  CHECK(read(dir.path / "fonts/a.bin") == "aaa");  // it comes first, so it was under way
  CHECK(missing(manifest, dir.path).size() == 1);  // what arrived is listed already
  CHECK(tried(dir.path));

  fake.fail_path.clear();
  fake.fetched.clear();
  CHECK(install(manifest, dir.path, fake, nullptr));
  CHECK(fake.fetched == std::vector<std::string>{"sounds/pack/b.bin"});
  CHECK(missing(manifest, dir.path).empty());
}

void a_bad_file_does_not_stop_the_others() {
  TempDir dir;
  const std::array entries{a, b};
  const Manifest manifest{entries};
  Fake fake;
  serve_both(fake);
  fake.served["fonts/a.bin"] = "xxx";
  CHECK(!tried(dir.path));
  CHECK(install(manifest, dir.path, fake, nullptr).error() == Error::BadContent);
  CHECK(tried(dir.path));
  CHECK(read(dir.path / "sounds/pack/b.bin") == "bb");
  CHECK(missing(manifest, dir.path).size() == 1);
}

void no_answer_stops_soon_and_still_counts_as_tried() {
  TempDir dir;
  std::vector<std::string> paths;
  for (int i = 0; i < 100; ++i) paths.push_back("many/" + std::to_string(i) + ".bin");
  std::vector<Entry> entries;
  for (const std::string& path : paths) entries.push_back({path, 1, "x"});
  const Manifest manifest{entries};
  class Down final : public Platform {
   public:
    std::expected<std::vector<std::uint8_t>, Error> fetch(const Entry&, std::string_view, const OnBytes&) override {
      ++asked;
      return std::unexpected(Error::Network);
    }
    std::string sha256(std::span<const std::uint8_t>) override { return {}; }
    std::atomic<int> asked{0};
  } down;
  CHECK(install(manifest, dir.path, down, nullptr).error() == Error::Network);
  CHECK(down.asked <= 8);  // one for each worker at most
  CHECK(tried(dir.path));
  CHECK(missing(manifest, dir.path).size() == 100);
}

void a_file_is_asked_for_at_its_source() {
  TempDir dir;
  const Entry own{"sounds/pack/b.bin", 2, "bb", 0};
  const Entry other{"fonts/a.bin", 3, "aaa", 1};
  const std::array entries{own, other};
  const std::array sources{Source{"us@1/assets/", ""}, Source{"them@2/dist/", "fonts/"}};
  const Manifest manifest{entries, sources};
  CHECK(remote_path(manifest, own) == "us@1/assets/sounds/pack/b.bin");
  CHECK(remote_path(manifest, other) == "them@2/dist/a.bin");
  // With no sources, as for --asset-url, a file is at its own path.
  CHECK(remote_path(Manifest{entries}, other) == "fonts/a.bin");
}

void a_damaged_file_is_fetched_again() {
  TempDir dir;
  const std::array entries{a};
  const Manifest manifest{entries};
  Fake fake;
  serve_both(fake);
  CHECK(install(manifest, dir.path, fake, nullptr));

  // Deleted: the size check sees it.
  fs::remove(dir.path / "fonts/a.bin");
  CHECK(missing(manifest, dir.path).size() == 1);
  CHECK(install(manifest, dir.path, fake, nullptr));
  CHECK(read(dir.path / "fonts/a.bin") == "aaa");

  // Left by an older run with the right size and other content: the hash sees it.
  fs::remove(dir.path / ".installed");
  std::ofstream(dir.path / "fonts/a.bin", std::ios::binary) << "zzz";
  CHECK(install(manifest, dir.path, fake, nullptr));
  CHECK(read(dir.path / "fonts/a.bin") == "aaa");
}

void many_files_arrive_with_progress_in_order() {
  TempDir dir;
  // Paths and hashes outlive the entries, which only view them.
  std::vector<std::string> paths, bodies;
  for (int i = 0; i < 100; ++i) {
    paths.push_back("many/" + std::to_string(i) + ".bin");
    bodies.push_back(std::string(static_cast<std::size_t>(i) + 1, 'x'));
  }
  Fake fake;
  std::vector<Entry> entries;
  std::uint64_t expected = 0;
  for (std::size_t i = 0; i < paths.size(); ++i) {
    entries.push_back({paths[i], bodies[i].size(), bodies[i]});
    fake.served[paths[i]] = bodies[i];
    expected += bodies[i].size();
  }
  const Manifest manifest{entries};
  std::uint64_t last = 0;
  int calls = 0;
  CHECK(install(manifest, dir.path, fake, [&](std::uint64_t done, std::uint64_t total) {
    CHECK(total == expected);
    CHECK(done >= last && done <= total);
    last = done;
    ++calls;
  }));
  CHECK(last == expected);
  CHECK(calls >= 100);
  CHECK(fake.fetched.size() == 100);
  CHECK(missing(manifest, dir.path).empty());
  for (std::size_t i = 0; i < paths.size(); ++i) CHECK(read(dir.path / paths[i]) == bodies[i]);
}

}  // namespace

int main() {
  many_files_arrive_with_progress_in_order();
  installs_everything_once();
  a_new_manifest_fetches_only_what_changed();
  what_does_not_match_is_never_kept();
  a_failed_run_keeps_what_arrived();
  a_bad_file_does_not_stop_the_others();
  no_answer_stops_soon_and_still_counts_as_tried();
  a_file_is_asked_for_at_its_source();
  a_damaged_file_is_fetched_again();
  return crpp_test_result();
}
