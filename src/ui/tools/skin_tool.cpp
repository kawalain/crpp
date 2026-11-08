// For whoever writes a Skin Pack.
//   crpp_skin check <folder>   loads the pack in the folder as the program does, with each overlay its
//                              Themes name and the packs it extends in folders beside it, and says the
//                              first mistake as <file>:<line>: what, or what it loaded.
//   crpp_skin names [<screen>] lists the screens, or what the program offers on one: its @numbers and
//                              @texts to read, and the names of its hits, pictures, fields, slots,
//                              reports and notes.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "crpp/ui/app.hpp"

namespace fs = std::filesystem;
using namespace crpp;

namespace {

std::string contents_of(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

skin::ReadFile files_of(fs::path folder) {
  return [folder = std::move(folder)](std::string_view path) -> std::optional<std::string> {
    const fs::path file = folder / std::string(path);
    if (!fs::exists(file)) return std::nullopt;
    return contents_of(file);
  };
}

int check(const fs::path& given) {
  const fs::path folder = fs::absolute(given).lexically_normal();
  const auto read_with = [&](const std::string& overlay) {
    skin::LoadOptions options;
    options.overlay = overlay;
    // Beside it, even one of another Repository, which an author keeps a copy of to check against.
    options.parent = [&](const skin::ParentQuery& query) -> std::optional<skin::FoundPack> {
      const fs::path beside = folder.parent_path() / std::string(query.id);
      if (!fs::exists(beside / "pack.cskin")) return std::nullopt;
      return skin::FoundPack{files_of(beside), beside.generic_string()};
    };
    return skin::load_pack(files_of(folder), ui::screen_schemas(), options);
  };
  // A mistake is said from the folder as it was given, so that an editor can go to it.
  const std::string from = (given / "").generic_string();
  const auto report = [&](const std::expected<skin::Pack, std::string>& pack, const std::string& overlay) {
    const std::string with = overlay.empty() ? "" : " (with the overlay " + overlay + ")";
    if (!pack) {
      std::fprintf(stderr, "%s%s%s\n", from.c_str(), pack.error().c_str(), with.c_str());
      return false;
    }
    if (const auto missing = skin::missing_constant(*pack)) {
      std::fprintf(stderr, "%spack.cskin: no constant \"%.*s\", which the program reads%s\n", from.c_str(), static_cast<int>(missing->size()), missing->data(), with.c_str());
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

int names(std::string_view screen) {
  const auto schemas = ui::screen_schemas();
  if (screen.empty()) {
    for (const skin::ScreenSchema& schema : schemas) std::printf("%.*s\n", static_cast<int>(schema.screen.size()), schema.screen.data());
    return 0;
  }
  const auto found = std::ranges::find(schemas, screen, &skin::ScreenSchema::screen);
  if (found == schemas.end()) {
    std::fprintf(stderr, "no screen \"%.*s\"; crpp_skin names lists them\n", static_cast<int>(screen.size()), screen.data());
    return 1;
  }
  const auto list = [](const char* what, std::span<const skin::Name> names, const char* sigil) {
    if (names.empty()) return;
    std::printf("%s:", what);
    for (const skin::Name& name : names) std::printf(" %s%.*s", sigil, static_cast<int>(name.name.size()), name.name.data());
    std::printf("\n");
  };
  list("numbers", found->numbers, "@");
  list("texts", found->texts, "@");
  list("hits", found->hits, "");
  list("pictures", found->pictures, "");
  list("fields", found->fields, "");
  list("slots", found->slots, "");
  list("reports", found->reports, "");
  list("notes", found->notes, "");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string_view command = argc >= 2 ? argv[1] : "";
  if (argc == 3 && command == "check") return check(argv[2]);
  if ((argc == 2 || argc == 3) && command == "names") return names(argc == 3 ? argv[2] : "");
  std::fprintf(stderr, "usage: crpp_skin check <folder of a pack> | names [<screen>]\n");
  return 2;
}
