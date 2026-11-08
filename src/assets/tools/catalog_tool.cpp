// Writes a Repository's catalog, unsigned, from this repository's folders:
//
//   crpp_catalog <out-dir> --remote <remote_assets.txt> [--assets <dir>] [--skins <dir>]
//                [--ref <git ref>] [--default-skin <id>] [--key <hex>] [--copy <cache dir>]
//
// --assets lists its files in the shared group, --skins each folder with a pack.jsonc as a
// Skin Pack, and --remote the third-party files with the sources they come from. --ref names
// the commit or tag at which raw.githubusercontent.com serves this repository's files, the
// second source of each. --copy also puts every file in <out-dir>, the third-party ones from
// that cache, so that the folder can be served as it is. Signing is a separate step
// (.github/scripts/sign-catalog.ps1), so the key never reaches this program.
//
//   crpp_catalog --verify <catalog.txt> [--key <hex>]
//
// checks a signed catalog as the program will: its key must be this build's (or --key) and
// its signature must hold.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "crpp/assets/catalog.hpp"
#include "crpp/assets/windows_platform.hpp"
#include "crpp/store/json.hpp"
#include "crpp/update/built_in.hpp"

namespace fs = std::filesystem;
using namespace crpp;

namespace {

constexpr std::string_view this_repository = "https://raw.githubusercontent.com/kawalain/crpp/";

std::optional<std::string> read_text(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string generic(const fs::path& path) { return path.generic_string(); }

struct Options {
  fs::path out, remote, assets, skins, copy;
  std::string ref, default_skin, key;
};

std::optional<Options> parse_options(int argc, char** argv) {
  if (argc < 2) return std::nullopt;
  Options options{.out = argv[1]};
  for (int i = 2; i + 1 < argc; i += 2) {
    const std::string_view name = argv[i];
    const char* value = argv[i + 1];
    if (name == "--remote") options.remote = value;
    else if (name == "--assets") options.assets = value;
    else if (name == "--skins") options.skins = value;
    else if (name == "--copy") options.copy = value;
    else if (name == "--ref") options.ref = value;
    else if (name == "--default-skin") options.default_skin = value;
    else if (name == "--key") options.key = value;
    else return std::nullopt;
  }
  if ((argc - 2) % 2 != 0 || options.remote.empty()) return std::nullopt;
  return options;
}

class Writer {
 public:
  explicit Writer(const Options& options) : options_(options) {}

  bool add_local(const fs::path& root, std::string_view prefix, std::string_view group, std::string_view remote_prefix) {
    std::vector<fs::path> files;
    for (const auto& item : fs::recursive_directory_iterator(root)) {
      if (item.is_regular_file()) files.push_back(item.path());
    }
    std::ranges::sort(files);
    for (const fs::path& file : files) {
      const std::string relative = generic(fs::relative(file, root));
      if (relative == "README.md") continue;
      const auto text = read_text(file);
      if (!text) return fail("cannot read " + generic(file));
      assets::File entry{.group = std::string(group), .path = std::string(prefix) + relative, .size = text->size()};
      entry.sha256 = platform_.sha256({reinterpret_cast<const std::uint8_t*>(text->data()), text->size()});
      if (!options_.ref.empty()) {
        entry.source = "crpp";
        entry.remote = std::string(remote_prefix) + relative;
      }
      if (!copy(entry.path, *text)) return false;
      catalog_.files.push_back(std::move(entry));
    }
    return true;
  }

  bool add_skins() {
    std::vector<fs::path> folders;
    for (const auto& item : fs::directory_iterator(options_.skins)) {
      if (item.is_directory() && fs::exists(item.path() / "pack.jsonc")) folders.push_back(item.path());
    }
    std::ranges::sort(folders);
    for (const fs::path& folder : folders) {
      const auto text = read_text(folder / "pack.jsonc");
      const auto pack = text ? store::Json::parse(*text) : std::unexpected(store::JsonError::Syntax);
      const auto id = pack ? pack->find("id") : nullptr;
      const auto format = pack ? pack->find("format") : nullptr;
      const auto parent = pack ? pack->find("extends") : nullptr;
      if (!id || !id->as_string() || !format || !format->as_i64()) return fail("no id or format in " + generic(folder / "pack.jsonc"));
      assets::SkinPack skin{.id = std::string(*id->as_string()), .format = static_cast<int>(*format->as_i64())};
      if (parent && parent->as_string()) skin.parent = *parent->as_string();
      if (skin.id != generic(folder.filename())) return fail("the folder of pack " + skin.id + " has another name");
      const std::string prefix = "skins/" + skin.id + "/";
      catalog_.skins.push_back(skin);
      if (!add_local(folder, prefix, assets::skin_group(skin.id), prefix)) return false;
    }
    return true;
  }

  // Lines of "source <name> <url>" and "<source>[:<remote>] <path> <size> <sha256> [nopick | <group>]".
  bool add_remote() {
    const auto text = read_text(options_.remote);
    if (!text) return fail("cannot read " + generic(options_.remote));
    std::istringstream lines(*text);
    std::string line;
    while (std::getline(lines, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty() || line[0] == '#') continue;
      std::istringstream fields(line);
      std::string first, path, size, hash, extra;
      fields >> first >> path;
      if (first == "source") {
        catalog_.sources.push_back({path, [&] { std::string url; fields >> url; return url; }()});
        continue;
      }
      fields >> size >> hash >> extra;
      const std::size_t colon = first.find(':');
      const std::string source = first.substr(0, colon);
      if (!catalog_.source(source)) return fail("no source line for " + source);
      assets::File entry{.group = "shared", .path = path, .size = std::stoull(size), .sha256 = hash, .source = source,
                         .remote = colon == std::string::npos ? generic(fs::path(path).filename()) : first.substr(colon + 1)};
      if (extra == "nopick") entry.pick = false;
      else if (!extra.empty()) entry.group = extra;
      if (!options_.copy.empty()) {
        const auto bytes = read_text(options_.copy / path);
        if (!bytes || platform_.sha256({reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size()}) != hash) {
          return fail(generic(options_.copy / path) + " is missing or not the pinned file");
        }
        if (!copy(entry.path, *bytes)) return false;
      }
      catalog_.files.push_back(std::move(entry));
    }
    return true;
  }

  int finish() {
    if (!options_.key.empty()) {
      const auto key = update::parse_public_key(options_.key);
      if (!key) return fail("--key is not 128 hex digits"), 2;
      catalog_.key = *key;
    } else if (const auto key = update::built_in_key()) {
      catalog_.key = *key;
    }
    catalog_.default_skin = options_.default_skin;
    if (!options_.ref.empty()) catalog_.sources.insert(catalog_.sources.begin(), {"crpp", std::string(this_repository) + options_.ref + "/"});
    std::ranges::stable_sort(catalog_.files, {}, &assets::File::path);
    const std::string text = assets::write_catalog(catalog_);
    // What the program will refuse is refused here first.
    if (const auto parsed = assets::parse_catalog(text); !parsed) {
      return fail("the catalog would not parse, line " + std::to_string(parsed.error().line) + ": " + parsed.error().what), 1;
    }
    std::error_code ec;
    fs::create_directories(options_.out, ec);
    std::ofstream out(options_.out / assets::catalog_name, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    if (!out) return fail("cannot write the catalog"), 1;
    std::printf("%zu files, %zu skins\n", catalog_.files.size(), catalog_.skins.size());
    return 0;
  }

  bool failed() const { return failed_; }

 private:
  bool copy(const std::string& path, const std::string& bytes) {
    if (options_.copy.empty()) return true;
    const fs::path target = options_.out / path;
    std::error_code ec;
    fs::create_directories(target.parent_path(), ec);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    out << bytes;
    out.close();
    return out ? true : fail("cannot write " + generic(target));
  }

  bool fail(const std::string& what) {
    std::fprintf(stderr, "crpp_catalog: %s\n", what.c_str());
    failed_ = true;
    return false;
  }

  const Options& options_;
  assets::Catalog catalog_;
  assets::WindowsPlatform platform_;
  bool failed_ = false;
};

}  // namespace

int verify(int argc, char** argv) {
  const auto text = read_text(argv[2]);
  const auto key = argc == 5 && std::string_view(argv[3]) == "--key" ? update::parse_public_key(argv[4]) : update::built_in_key();
  if (!text || !key) {
    std::fprintf(stderr, "crpp_catalog: no catalog, or no key to check it with\n");
    return 2;
  }
  const auto catalog = assets::parse_catalog(*text);
  if (!catalog) {
    std::fprintf(stderr, "crpp_catalog: line %zu: %s\n", catalog.error().line, catalog.error().what.c_str());
    return 1;
  }
  assets::WindowsPlatform platform;
  const std::span<const std::uint8_t> signed_part(reinterpret_cast<const std::uint8_t*>(text->data()), catalog->signed_bytes);
  if (catalog->key != *key || !catalog->signature || !platform.verify(*key, signed_part, *catalog->signature)) {
    std::fprintf(stderr, "crpp_catalog: not signed by the key\n");
    return 1;
  }
  std::printf("signed by the key, %zu files\n", catalog->files.size());
  return 0;
}

int main(int argc, char** argv) {
  if (argc >= 3 && std::string_view(argv[1]) == "--verify") return verify(argc, argv);
  const auto options = parse_options(argc, argv);
  if (!options) {
    std::fprintf(stderr, "usage: crpp_catalog <out-dir> --remote <file> [--assets <dir>] [--skins <dir>] [--ref <ref>] "
                         "[--default-skin <id>] [--key <hex>] [--copy <cache dir>]\n");
    return 2;
  }
  Writer writer(*options);
  // Remote first: it holds the source lines the others' entries do not need.
  if (!writer.add_remote()) return 1;
  if (!options->assets.empty() && !writer.add_local(options->assets, "", "shared", "assets/")) return 1;
  if (!options->skins.empty() && !writer.add_skins()) return 1;
  return writer.finish();
}
