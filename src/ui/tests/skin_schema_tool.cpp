// For whoever writes a Skin Pack.
//   crpp_skin_schema <folder>           writes the JSON Schemas into the folder
//   crpp_skin_schema --check <folder>   says whether the folder holds exactly those, as a test does
//   crpp_skin_schema --pack <folder>    loads a pack as the program does and says what it refuses
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "crpp/skin/schema.hpp"
#include "crpp/ui/app.hpp"

namespace fs = std::filesystem;
using namespace crpp;

namespace {

std::map<std::string, std::string> wanted() {
  std::map<std::string, std::string> files;
  files["common.schema.json"] = skin::common_json_schema();
  files["pack.schema.json"] = skin::pack_json_schema();
  for (const skin::ScreenSchema& schema : ui::screen_schemas()) files["screen." + std::string(schema.screen) + ".schema.json"] = skin::screen_json_schema(schema);
  return files;
}

std::string contents_of(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

int write_all(const fs::path& folder) {
  fs::create_directories(folder);
  for (const auto& [name, text] : wanted()) std::ofstream(folder / name, std::ios::binary) << text;
  return 0;
}

int check_all(const fs::path& folder) {
  int wrong = 0;
  const auto files = wanted();
  for (const auto& [name, text] : files) {
    if (contents_of(folder / name) != text) {
      std::fprintf(stderr, "%s is out of date\n", name.c_str());
      ++wrong;
    }
  }
  for (const auto& entry : fs::directory_iterator(folder)) {
    const std::string name = entry.path().filename().string();
    if (!files.contains(name)) {
      std::fprintf(stderr, "%s is not one of them\n", name.c_str());
      ++wrong;
    }
  }
  if (wrong) std::fprintf(stderr, "Run crpp_skin_schema %s to write them again.\n", folder.string().c_str());
  return wrong ? 1 : 0;
}

int check_pack(const fs::path& folder) {
  const auto read = [&](std::string_view path) -> std::optional<std::string> {
    const fs::path file = folder / std::string(path);
    if (!fs::exists(file)) return std::nullopt;
    return contents_of(file);
  };
  const auto pack = skin::load_pack(read, ui::screen_schemas());
  if (!pack) {
    std::fprintf(stderr, "%s\n", pack.error().c_str());
    return 1;
  }
  if (const auto missing = skin::missing_constant(*pack)) {
    std::fprintf(stderr, "pack.jsonc: no constant \"%.*s\"\n", static_cast<int>(missing->size()), missing->data());
    return 1;
  }
  std::printf("%s: %zu screens of %zu, %zu nodes\n", pack->id.c_str(), pack->screens.size(), ui::screen_schemas().size(), pack->nodes.size());
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string first = argc >= 2 ? argv[1] : "";
  if (argc == 2 && first[0] != '-') return write_all(argv[1]);
  if (argc == 3 && first == "--check") return check_all(argv[2]);
  if (argc == 3 && first == "--pack") return check_pack(argv[2]);
  std::fprintf(stderr, "usage: crpp_skin_schema <folder> | --check <folder> | --pack <folder>\n");
  return 2;
}
