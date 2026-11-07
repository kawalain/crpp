// For whoever writes a Skin Pack.
//   crpp_skin_schema <folder>           writes the JSON Schemas into the folder
//   crpp_skin_schema --check <folder>   says whether the folder holds exactly those, as a test does
//   crpp_skin_schema --pack <folder>    loads a pack as the program does and says what it refuses
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

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

skin::ReadFile files_of(fs::path folder) {
  return [folder = std::move(folder)](std::string_view path) -> std::optional<std::string> {
    const fs::path file = folder / std::string(path);
    if (!fs::exists(file)) return std::nullopt;
    return contents_of(file);
  };
}

// As the program reads it: with each overlay its Themes name, and what it extends in a folder beside it.
int check_pack(const fs::path& given) {
  const fs::path folder = fs::absolute(given).lexically_normal();
  const auto read_with = [&](const std::string& overlay) {
    skin::LoadOptions options;
    options.overlay = overlay;
    options.parent = [&](std::string_view id) -> std::optional<skin::ReadFile> {
      const fs::path beside = folder.parent_path() / std::string(id);
      if (!fs::exists(beside / "pack.jsonc")) return std::nullopt;
      return files_of(beside);
    };
    return skin::load_pack(files_of(folder), ui::screen_schemas(), options);
  };
  const auto report = [](const std::expected<skin::Pack, std::string>& pack, const std::string& overlay) {
    if (!pack) {
      std::fprintf(stderr, "%s%s%s\n", overlay.c_str(), overlay.empty() ? "" : ": ", pack.error().c_str());
      return false;
    }
    if (const auto missing = skin::missing_constant(*pack)) {
      std::fprintf(stderr, "%s%spack.jsonc: no constant \"%.*s\"\n", overlay.c_str(), overlay.empty() ? "" : ": ", static_cast<int>(missing->size()), missing->data());
      return false;
    }
    std::printf("%s%s%s: %zu screens of %zu, %zu nodes\n", pack->id.c_str(), overlay.empty() ? "" : " with ", overlay.c_str(), pack->screens.size(), ui::screen_schemas().size(), pack->nodes.size());
    return true;
  };
  const auto plain = read_with({});
  if (!report(plain, {})) return 1;
  std::vector<std::string> done;
  for (const skin::PackTheme& theme : plain->themes) {
    if (theme.overlay.empty() || std::ranges::find(done, theme.overlay) != done.end()) continue;
    done.push_back(theme.overlay);
    if (!report(read_with(theme.overlay), theme.overlay)) return 1;
  }
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
