#include "crpp/store/windows_platform.hpp"

#include <windows.h>

#include <fstream>
#include <iterator>
#include <random>
#include <string>

#include "check.hpp"

using namespace crpp::store;
namespace fs = std::filesystem;

namespace {

std::string read(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

fs::path make_temp_dir() {
  std::random_device rd;
  const fs::path dir = fs::temp_directory_path() / ("crpp_platform_test_" + std::to_string(rd()) + std::to_string(rd()));
  fs::create_directories(dir);
  return dir;
}

void protection_round_trips() {
  WindowsPlatform platform;
  const std::string secret = "the secret token \xEA\xB5\xAC\xEB\xA6\x84";
  const auto blob = platform.protect(secret);
  CHECK(blob.has_value());
  CHECK(blob->find("the secret") == std::string::npos);
  const auto plain = platform.unprotect(*blob);
  CHECK(plain.has_value());
  CHECK(*plain == secret);

  const auto empty = platform.protect("");
  CHECK(empty.has_value());
  CHECK(platform.unprotect(*empty) == std::string{});
}

void garbage_does_not_decrypt() {
  WindowsPlatform platform;
  CHECK(!platform.unprotect("not a DPAPI blob").has_value());
  CHECK(!platform.unprotect("").has_value());
  auto blob = platform.protect("secret");
  CHECK(blob.has_value());
  blob->resize(blob->size() / 2);
  CHECK(!platform.unprotect(*blob).has_value());
}

void replace_creates_and_overwrites() {
  WindowsPlatform platform;
  const fs::path dir = make_temp_dir();
  const fs::path file = dir / "구름.json";
  CHECK(platform.replace_file(file, "first").has_value());
  CHECK(read(file) == "first");
  CHECK(platform.replace_file(file, "second, longer").has_value());
  CHECK(read(file) == "second, longer");
  CHECK(platform.replace_file(file, "").has_value());
  CHECK(read(file).empty());
  CHECK(!fs::exists(dir / "구름.json.tmp"));
  fs::remove_all(dir);
}

void failure_keeps_the_old_file() {
  WindowsPlatform platform;
  const fs::path dir = make_temp_dir();
  const fs::path file = dir / "settings.json";
  CHECK(platform.replace_file(file, "old").has_value());

  HANDLE held = CreateFileW(file.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  CHECK(held != INVALID_HANDLE_VALUE);
  CHECK(platform.replace_file(file, "new").error() == Error::Write);
  CloseHandle(held);
  CHECK(read(file) == "old");
  fs::path temp = file;
  temp += ".tmp";
  CHECK(!fs::exists(temp));

  CHECK(!platform.replace_file(dir / "missing" / "settings.json", "x").has_value());
  fs::remove_all(dir);
}

void folders_are_under_local_app_data() {
  const auto locations = default_locations();
  CHECK(locations.has_value());
  CHECK(locations->protected_dir.filename() == "crpp");
  CHECK(locations->shared_dir.filename() == "Cloudroom");
  CHECK(locations->protected_dir.parent_path() == locations->shared_dir.parent_path());
  CHECK(locations->protected_dir.is_absolute());
}

void store_works_on_the_real_platform() {
  WindowsPlatform platform;
  const fs::path dir = make_temp_dir();
  const Locations locations{dir / "crpp", dir / "Cloudroom"};
  {
    auto store = Store::open(locations, platform, 1000);
    store.set("token", Json("SECRET"));
    store.record_chat("ABC234", {1, "c", "n", "", "SECRET", 5});
    CHECK(store.save().has_value());
  }
  CHECK(read(dir / "crpp" / "secrets.bin").find("SECRET") == std::string::npos);
  CHECK(read(dir / "crpp" / "chat.bin").find("SECRET") == std::string::npos);
  auto store = Store::open(locations, platform, 1000);
  CHECK(!store.secrets_lost());
  CHECK(store.string_or("token", "") == "SECRET");
  CHECK(store.enter_room("ABC234").size() == 1);
  fs::remove_all(dir);
}

}  // namespace

int main() {
  protection_round_trips();
  garbage_does_not_decrypt();
  replace_creates_and_overwrites();
  failure_keeps_the_old_file();
  folders_are_under_local_app_data();
  store_works_on_the_real_platform();
  return crpp_test_result();
}
