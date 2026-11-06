// The shell's parts that need neither a window nor a store.
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "check.hpp"
// clang-format off
#include "notification_sounds.hpp"
// clang-format on

using namespace crpp::gui;
namespace fs = std::filesystem;

namespace {

std::string read_all(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}

fs::path fresh_dir(const char* name) {
  const fs::path dir = fs::temp_directory_path() / name;
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir;
}

std::string lower_id(std::string_view stem) {
  stem.remove_prefix(stem.find('_') + 1);
  return std::string(stem);
}

void test_finds_the_shipped_sounds_in_order(const char* folder) {
  const auto sounds = find_notification_sounds(folder, lower_id);
  CHECK(sounds.size() == 2);
  if (sounds.size() != 2) return;
  CHECK(sounds[0].stem == "01_cloud_chat");
  CHECK(sounds[0].id == "cloud_chat");
  CHECK(sounds[0].name == "메시지");
  CHECK(sounds[1].stem == "02_cloud_enter");
  CHECK(sounds[1].name == "구름 입장");
}

void test_a_sound_without_a_label_uses_its_id() {
  const fs::path dir = fresh_dir("crpp_sounds_test");
  std::ofstream(dir / "03_b.wav") << "x";
  std::ofstream(dir / "01_a.wav") << "x";
  std::ofstream(dir / "notes.txt") << "x";
  const auto sounds = find_notification_sounds(dir, lower_id);  // no pack.json either
  CHECK(sounds.size() == 2);
  if (sounds.size() == 2) {
    CHECK(sounds[0].name == "a");
    CHECK(sounds[1].name == "b");
  }
  fs::remove_all(dir);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1) test_finds_the_shipped_sounds_in_order(argv[1]);
  test_a_sound_without_a_label_uses_its_id();
  return crpp_test_result();
}
