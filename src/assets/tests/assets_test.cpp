#include "crpp/assets/assets.hpp"

#include <array>
#include <fstream>
#include <iterator>
#include <map>
#include <random>

#include "check.hpp"

using namespace crpp::assets;
namespace fs = std::filesystem;

namespace {

// The "hash" is the content itself, which keeps the expectations readable.
class Fake final : public Platform {
 public:
  std::expected<std::vector<std::uint8_t>, Error> fetch(const Entry& entry, const OnBytes& on_bytes) override {
    fetched.emplace_back(entry.path);
    if (fail_at && fetched.size() == fail_at) return std::unexpected(Error::Network);
    const std::string& body = served[std::string(entry.path)];
    if (on_bytes) on_bytes(body.size() / 2);
    return std::vector<std::uint8_t>(body.begin(), body.end());
  }
  std::string sha256(std::span<const std::uint8_t> bytes) override { return {bytes.begin(), bytes.end()}; }

  std::map<std::string, std::string> served;
  std::vector<std::string> fetched;
  std::size_t fail_at = 0;
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

Fake serving_both() {
  Fake fake;
  fake.served["fonts/a.bin"] = "aaa";
  fake.served["sounds/pack/b.bin"] = "bb";
  return fake;
}

void installs_everything_once() {
  TempDir dir;
  const std::array entries{a, b};
  const Manifest manifest{"c", entries};
  CHECK(missing(manifest, dir.path).size() == 2);

  Fake fake = serving_both();
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
  Fake fake = serving_both();
  CHECK(install(Manifest{"c", entries}, dir.path, fake, nullptr));

  const Entry changed{"fonts/a.bin", 4, "AAAA"};
  const std::array next{changed};
  const Manifest manifest{"d", next};
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
  const Manifest manifest{"c", entries};
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
  const Manifest manifest{"c", entries};
  Fake fake = serving_both();
  fake.fail_at = 2;
  CHECK(install(manifest, dir.path, fake, nullptr).error() == Error::Network);
  CHECK(read(dir.path / "fonts/a.bin") == "aaa");
  CHECK(missing(manifest, dir.path).size() == 2);  // not listed as installed yet

  fake.fail_at = 0;
  fake.fetched.clear();
  CHECK(install(manifest, dir.path, fake, nullptr));
  CHECK(fake.fetched == std::vector<std::string>{"sounds/pack/b.bin"});
  CHECK(missing(manifest, dir.path).empty());
}

void a_damaged_file_is_fetched_again() {
  TempDir dir;
  const std::array entries{a};
  const Manifest manifest{"c", entries};
  Fake fake = serving_both();
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

}  // namespace

int main() {
  installs_everything_once();
  a_new_manifest_fetches_only_what_changed();
  what_does_not_match_is_never_kept();
  a_failed_run_keeps_what_arrived();
  a_damaged_file_is_fetched_again();
  return crpp_test_result();
}
