#include "crpp/assets/catalog.hpp"

#include <algorithm>
#include <charconv>
#include <unordered_set>

namespace crpp::assets {

namespace {

constexpr std::size_t max_path = 200;
constexpr std::size_t max_id = 32;
constexpr std::size_t max_url = 300;

std::vector<std::string_view> fields_of(std::string_view line) {
  std::vector<std::string_view> fields;
  while (true) {
    const std::size_t space = line.find(' ');
    fields.push_back(line.substr(0, space));
    if (space == std::string_view::npos) return fields;
    line.remove_prefix(space + 1);
  }
}

constexpr std::size_t max_words = 64;

// The rest of a line after its first `skip` fields: a name, with no control characters.
std::optional<std::string_view> words_after(std::string_view line, std::size_t skip) {
  for (std::size_t i = 0; i < skip; ++i) {
    const std::size_t space = line.find(' ');
    if (space == std::string_view::npos) return std::nullopt;
    line.remove_prefix(space + 1);
  }
  if (line.empty() || line.size() > max_words || line.front() == ' ' || line.back() == ' ') return std::nullopt;
  if (!std::ranges::all_of(line, [](char c) { return static_cast<unsigned char>(c) >= 0x20 && c != 0x7f; })) return std::nullopt;
  return line;
}

bool has_name(const Names& names, std::string_view locale) {
  return std::ranges::any_of(names, [&](const auto& name) { return name.first == locale; });
}

}  // namespace

bool is_id(std::string_view s) {
  return !s.empty() && s.size() <= max_id && std::ranges::all_of(s, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
  });
}

namespace {

bool is_hex(std::string_view s, std::size_t length) {
  return s.size() == length && std::ranges::all_of(s, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

// Relative, with no empty, "." or ".." part, so it stays inside the assets folder.
bool is_path(std::string_view s) {
  if (s.empty() || s.size() > max_path) return false;
  const bool plain = std::ranges::all_of(s, [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '/';
  });
  if (!plain) return false;
  while (true) {
    const std::size_t slash = s.find('/');
    const std::string_view part = s.substr(0, slash);
    if (part.empty() || part == "." || part == "..") return false;
    if (slash == std::string_view::npos) return true;
    s.remove_prefix(slash + 1);
  }
}

// Digits with no leading zero, `parts` of them between dots.
bool dotted(std::string_view s, std::size_t parts) {
  for (std::size_t i = 0; i < parts; ++i) {
    const std::size_t dot = s.find('.');
    const std::string_view part = s.substr(0, dot);
    if (part.empty() || part.size() > 9 || (part.size() > 1 && part[0] == '0') || !std::ranges::all_of(part, [](char c) { return c >= '0' && c <= '9'; })) return false;
    if (i + 1 == parts) return dot == std::string_view::npos;
    if (dot == std::string_view::npos) return false;
    s.remove_prefix(dot + 1);
  }
  return false;
}

bool is_version(std::string_view s) { return s.starts_with("0.") && dotted(s, 3); }
bool is_version_pin(std::string_view s) { return s.starts_with("0.") && (dotted(s, 2) || dotted(s, 3)); }

bool is_local_host(std::string_view rest) {
  return rest.starts_with("127.0.0.1:") || rest.starts_with("127.0.0.1/") || rest.starts_with("localhost:") ||
         rest.starts_with("localhost/");
}

// HTTPS, or plain HTTP to this machine for tests. The hashes protect the files either way.
bool is_url(std::string_view s) {
  if (s.size() > max_url || !s.ends_with('/')) return false;
  if (!std::ranges::all_of(s, [](char c) { return c > ' ' && c < 127; })) return false;
  if (s.starts_with("https://")) return s.size() > 9;
  return s.starts_with("http://") && is_local_host(s.substr(7));
}

constexpr std::string_view sound_kind_names[] = {"keyboard", "mouse", "pen"};

bool is_group(std::string_view s) {
  if (s == "shared") return true;
  if (s.starts_with("skin:")) return is_id(s.substr(5));
  if (s.starts_with("sound:")) return is_id(s.substr(6));
  return false;
}

template <typename T>
bool number(std::string_view s, T& out) {
  if (s.empty() || (s.size() > 1 && s[0] == '0')) return false;
  const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), out);
  return error == std::errc{} && end == s.data() + s.size();
}

std::optional<update::Signature> parse_signature(std::string_view hex) {
  if (!is_hex(hex, 128)) return std::nullopt;
  update::Signature signature{};
  for (std::size_t i = 0; i < signature.size(); ++i) {
    std::from_chars(hex.data() + i * 2, hex.data() + i * 2 + 2, signature[i], 16);
  }
  return signature;
}

std::string to_hex(std::span<const std::uint8_t> bytes) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string text;
  text.reserve(bytes.size() * 2);
  for (const std::uint8_t b : bytes) {
    text += digits[b >> 4];
    text += digits[b & 15];
  }
  return text;
}

}  // namespace

const SourceUrl* Catalog::source(std::string_view name) const {
  const auto it = std::ranges::find(sources, name, &SourceUrl::name);
  return it == sources.end() ? nullptr : &*it;
}

const SkinPack* Catalog::skin(std::string_view id) const {
  const auto it = std::ranges::find(skins, id, &SkinPack::id);
  return it == skins.end() ? nullptr : &*it;
}

const SoundPack* Catalog::sound(std::string_view id) const {
  const auto it = std::ranges::find(sounds, id, &SoundPack::id);
  return it == sounds.end() ? nullptr : &*it;
}

const CatalogTheme* SkinPack::theme(std::string_view theme_id) const {
  const auto it = std::ranges::find(themes, theme_id, &CatalogTheme::id);
  return it == themes.end() ? nullptr : &*it;
}

std::expected<Catalog, CatalogError> parse_catalog(std::string_view text) {
  if (text.size() > max_catalog_size) return std::unexpected(CatalogError{0, "larger than 4 MB"});
  if (!text.ends_with('\n')) return std::unexpected(CatalogError{0, "does not end with a line break"});
  Catalog catalog;
  std::unordered_set<std::string_view> paths;
  std::size_t number_of_line = 0, offset = 0;
  const auto fail = [&](std::string what) { return std::unexpected(CatalogError{number_of_line, std::move(what)}); };
  const auto skin_of = [&](std::string_view id) -> SkinPack* {
    const auto it = std::ranges::find(catalog.skins, id, &SkinPack::id);
    return it == catalog.skins.end() ? nullptr : &*it;
  };
  while (offset < text.size()) {
    const std::size_t end = text.find('\n', offset);
    const std::string_view line = text.substr(offset, end - offset);
    const std::size_t line_start = offset;
    offset = end + 1;
    ++number_of_line;
    if (catalog.signature) return fail("text after the signature");
    if (line.find('\r') != std::string_view::npos) return fail("a carriage return");
    const auto f = fields_of(line);
    if (number_of_line == 1) {
      if (line != "crpp-catalog 1") return fail("not a catalog of format 1");
      continue;
    }
    if (number_of_line == 2) {
      const auto key = f.size() == 2 && f[0] == "key" && is_hex(f[1], 128) ? update::parse_public_key(f[1]) : std::nullopt;
      if (!key) return fail("the second line must be the key");
      catalog.key = *key;
      continue;
    }
    if (f[0] == "signature") {
      if (f.size() != 2) return fail("a signature has one field");
      catalog.signature = parse_signature(f[1]);
      if (!catalog.signature) return fail("a signature is 128 lowercase hex digits");
      catalog.signed_bytes = line_start;
    } else if (f[0] == "source") {
      if (f.size() != 3 || !is_id(f[1]) || !is_url(f[2])) return fail("a source is a name and an address ending with '/'");
      if (catalog.source(f[1])) return fail("a second source of that name");
      catalog.sources.push_back({std::string(f[1]), std::string(f[2])});
    } else if (f[0] == "skin") {
      SkinPack skin;
      if (f.size() < 3 || f.size() > 4 || !is_id(f[1]) || !number(f[2], skin.format) || skin.format < 1) {
        return fail("a skin is an id, a format and maybe a parent");
      }
      if (catalog.skin(f[1])) return fail("a second skin of that id");
      if (f.size() == 4 && (!is_id(f[3]) || f[3] == f[1])) return fail("a parent is another skin's id");
      skin.id = f[1];
      if (f.size() == 4) skin.parent = f[3];
      catalog.skins.push_back(std::move(skin));
    } else if (f[0] == "skin-version") {
      SkinPack* skin = f.size() == 3 ? skin_of(f[1]) : nullptr;
      if (!skin || !is_version(f[2])) return fail("a skin-version is a skin listed above and 0.y.z");
      if (!skin->version.empty()) return fail("a second version of that skin");
      skin->version = f[2];
    } else if (f[0] == "parent") {
      SkinPack* skin = f.size() >= 3 && f.size() % 2 == 1 ? skin_of(f[1]) : nullptr;
      if (!skin || !is_id(f[2]) || f[2] == f[1] || skin->pin) return fail("a parent is a skin listed above, the id it extends and its pins");
      ParentPin pin{.id = std::string(f[2])};
      for (std::size_t i = 3; i + 1 < f.size(); i += 2) {
        std::string* field = f[i] == "fingerprint" ? &pin.fingerprint : f[i] == "repository" ? &pin.repository
                           : f[i] == "version"     ? &pin.version
                           : f[i] == "sha256"      ? &pin.sha256
                                                   : nullptr;
        if (!field || !field->empty()) return fail("a parent's pins are fingerprint, repository, version and sha256, once each");
        *field = f[i + 1];
      }
      const bool good = (pin.fingerprint.empty() || is_hex(pin.fingerprint, 64)) && (pin.sha256.empty() || is_hex(pin.sha256, 64)) &&
                        (pin.version.empty() || is_version_pin(pin.version)) && (pin.repository.empty() || (!pin.fingerprint.empty() && pin.repository.size() <= max_url));
      if (!good) return fail("a parent's fingerprint and sha256 are 64 hex digits, its version 0.y or 0.y.z, and a repository needs a fingerprint");
      if (!skin->parent.empty() && (skin->parent != pin.id || !pin.fingerprint.empty())) return fail("a parent is the one the skin line names");
      skin->pin = std::move(pin);
    } else if (f[0] == "skin-name") {
      SkinPack* skin = f.size() >= 4 ? skin_of(f[1]) : nullptr;
      const auto words = words_after(line, 3);
      if (!skin || !is_id(f[2]) || !words) return fail("a skin-name is a skin listed above, a locale and words");
      if (has_name(skin->names, f[2])) return fail("a second name in that locale");
      skin->names.emplace_back(f[2], *words);
    } else if (f[0] == "theme") {
      SkinPack* skin = f.size() >= 3 ? skin_of(f[1]) : nullptr;
      if (!skin || f.size() > 4 || !is_id(f[2]) || (f.size() == 4 && f[3] != "dark")) {
        return fail("a theme is a skin listed above, an id and maybe dark");
      }
      if (skin->theme(f[2])) return fail("a second theme of that id");
      skin->themes.push_back({std::string(f[2]), f.size() == 4, {}});
    } else if (f[0] == "theme-name") {
      SkinPack* skin = f.size() >= 5 ? skin_of(f[1]) : nullptr;
      CatalogTheme* theme = nullptr;
      if (skin) {
        for (CatalogTheme& each : skin->themes) {
          if (each.id == f[2]) theme = &each;
        }
      }
      const auto words = words_after(line, 4);
      if (!theme || !is_id(f[3]) || !words) return fail("a theme-name is a theme listed above, a locale and words");
      if (has_name(theme->names, f[3])) return fail("a second name in that locale");
      theme->names.emplace_back(f[3], *words);
    } else if (f[0] == "sound") {
      if (f.size() != 2 || !is_id(f[1])) return fail("a sound is an id");
      if (catalog.sound(f[1])) return fail("a second sound of that id");
      catalog.sounds.push_back({std::string(f[1]), {}});
    } else if (f[0] == "sound-name") {
      const auto it = f.size() >= 4 ? std::ranges::find(catalog.sounds, f[1], &SoundPack::id) : catalog.sounds.end();
      const auto words = words_after(line, 3);
      if (it == catalog.sounds.end() || !is_id(f[2]) || !words) return fail("a sound-name is a sound listed above, a locale and words");
      if (has_name(it->names, f[2])) return fail("a second name in that locale");
      it->names.emplace_back(f[2], *words);
    } else if (f[0] == "default-skin") {
      if (f.size() != 2 || !is_id(f[1]) || !catalog.default_skin.empty()) return fail("one default skin, by id");
      catalog.default_skin = f[1];
    } else if (f[0] == "file") {
      File file;
      if (f.size() < 5 || !is_group(f[1]) || !is_path(f[2]) || !number(f[3], file.size) || !is_hex(f[4], 64)) {
        return fail("a file is a group, a path, a size and a SHA-256");
      }
      if (file.size > max_file_size) return fail("a file larger than 64 MB");
      std::size_t next = 5;
      if (next < f.size() && f[next] == "from") {
        if (next + 2 >= f.size()) return fail("from needs a source and a path");
        if (!catalog.source(f[next + 1])) return fail("from names a source not listed above");
        if (!is_path(f[next + 2])) return fail("from needs a path");
        file.source = f[next + 1];
        file.remote = f[next + 2];
        next += 3;
      }
      if (next < f.size() && f[next] == "nopick") {
        file.pick = false;
        ++next;
      }
      if (next != f.size()) return fail("unknown fields after a file");
      if (!paths.insert(f[2]).second) return fail("a second file at that path");
      file.group = f[1];
      file.path = f[2];
      file.sha256 = f[4];
      catalog.files.push_back(std::move(file));
    }
    // Any other line is of a kind a later catalog brings, and is skipped.
  }
  if (number_of_line < 2) return std::unexpected(CatalogError{0, "no key"});
  for (const SkinPack& skin : catalog.skins) {
    if (!skin.parent.empty() && !catalog.skin(skin.parent)) return std::unexpected(CatalogError{0, "skin " + skin.id + " extends one not listed"});
  }
  // A chain longer than the list of skins goes round in a circle.
  for (const SkinPack& skin : catalog.skins) {
    std::string_view at = skin.id;
    for (std::size_t steps = 0; !at.empty(); ++steps) {
      if (steps > catalog.skins.size()) return std::unexpected(CatalogError{0, "skin " + skin.id + " extends itself"});
      at = catalog.skin(at)->parent;
    }
  }
  if (!catalog.default_skin.empty() && !catalog.skin(catalog.default_skin)) return std::unexpected(CatalogError{0, "the default skin is not listed"});
  for (const File& file : catalog.files) {
    if (file.group.starts_with("skin:") && !catalog.skin(std::string_view(file.group).substr(5))) {
      return std::unexpected(CatalogError{0, file.path + " is in a skin not listed"});
    }
  }
  return catalog;
}

std::string write_catalog(const Catalog& catalog) {
  std::string text = "crpp-catalog 1\nkey " + to_hex(catalog.key) + "\n";
  for (const SourceUrl& source : catalog.sources) text += "source " + source.name + " " + source.url + "\n";
  for (const SkinPack& skin : catalog.skins) {
    text += "skin " + skin.id + " " + std::to_string(skin.format) + (skin.parent.empty() ? "" : " " + skin.parent) + "\n";
    if (!skin.version.empty()) text += "skin-version " + skin.id + " " + skin.version + "\n";
    if (const auto& pin = skin.pin) {
      text += "parent " + skin.id + " " + pin->id;
      if (!pin->fingerprint.empty()) text += " fingerprint " + pin->fingerprint;
      if (!pin->repository.empty()) text += " repository " + pin->repository;
      if (!pin->version.empty()) text += " version " + pin->version;
      if (!pin->sha256.empty()) text += " sha256 " + pin->sha256;
      text += "\n";
    }
    for (const auto& [locale, words] : skin.names) text += "skin-name " + skin.id + " " + locale + " " + words + "\n";
    for (const CatalogTheme& theme : skin.themes) {
      text += "theme " + skin.id + " " + theme.id + (theme.dark ? " dark" : "") + "\n";
      for (const auto& [locale, words] : theme.names) text += "theme-name " + skin.id + " " + theme.id + " " + locale + " " + words + "\n";
    }
  }
  if (!catalog.default_skin.empty()) text += "default-skin " + catalog.default_skin + "\n";
  for (const SoundPack& sound : catalog.sounds) {
    text += "sound " + sound.id + "\n";
    for (const auto& [locale, words] : sound.names) text += "sound-name " + sound.id + " " + locale + " " + words + "\n";
  }
  for (const File& file : catalog.files) {
    text += "file " + file.group + " " + file.path + " " + std::to_string(file.size) + " " + file.sha256;
    if (!file.source.empty()) text += " from " + file.source + " " + file.remote;
    if (!file.pick) text += " nopick";
    text += "\n";
  }
  return text;
}

std::vector<std::string> skin_chain(const Catalog& catalog, std::string_view id) {
  std::vector<std::string> chain;
  for (const SkinPack* skin = catalog.skin(id); skin != nullptr && chain.size() <= catalog.skins.size();
       skin = skin->parent.empty() ? nullptr : catalog.skin(skin->parent)) {
    chain.push_back(skin->id);
  }
  return chain;
}

namespace {

bool github_name(std::string_view s, bool dots) {
  if (s.empty() || s.size() > 100 || s == "." || s == "..") return false;
  return std::ranges::all_of(s, [&](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || (dots && (c == '.' || c == '_'));
  });
}

}  // namespace

std::string folder_of(std::string_view address) {
  while (!address.empty() && (address.front() == ' ' || address.front() == '\t')) address.remove_prefix(1);
  while (!address.empty() && (address.back() == ' ' || address.back() == '\t')) address.remove_suffix(1);
  if (address.starts_with("github:")) {
    address.remove_prefix(7);
    std::string_view ref = "HEAD";
    if (const std::size_t at = address.find('@'); at != std::string_view::npos) {
      ref = address.substr(at + 1);
      address = address.substr(0, at);
    }
    const std::size_t slash = address.find('/');
    if (slash == std::string_view::npos) return {};
    const std::string_view owner = address.substr(0, slash), repo = address.substr(slash + 1);
    // A ref may hold slashes, as a branch does, but stays a path of its own.
    if (!github_name(owner, false) || !github_name(repo, true) || !is_path(ref)) return {};
    return "https://raw.githubusercontent.com/" + std::string(owner) + "/" + std::string(repo) + "/" + std::string(ref) + "/";
  }
  // The address of the catalog itself names its folder too.
  if (address.ends_with(catalog_name) && address.size() > catalog_name.size() && address[address.size() - catalog_name.size() - 1] == '/') {
    address.remove_suffix(catalog_name.size());
  }
  std::string folder(address);
  if (!folder.ends_with('/')) folder += '/';
  return is_url(folder) ? folder : std::string();
}

bool offers_only_packs(const Catalog& catalog) {
  return std::ranges::all_of(catalog.files, [](const File& file) {
    const std::string_view group = file.group;
    if (group.starts_with("skin:")) return file.path.starts_with("skins/" + std::string(group.substr(5)) + "/");
    if (group.starts_with("sound:")) {
      const std::string id = "/" + std::string(group.substr(6)) + "/";
      return std::ranges::any_of(sound_kind_names, [&](std::string_view kind) { return file.path.starts_with("sounds/" + std::string(kind) + id); });
    }
    return false;
  });
}

std::string skin_group(std::string_view id) { return "skin:" + std::string(id); }

bool version_matches(std::string_view pin, std::string_view version) {
  if (pin.empty()) return true;
  if (version == pin) return true;
  // 0.3 takes 0.3.z: the version goes on past the pin with a dot.
  return std::ranges::count(pin, '.') == 1 && version.size() > pin.size() && version.starts_with(pin) && version[pin.size()] == '.';
}

std::string digest_text(const Catalog& catalog, std::string_view skin) {
  const std::string group = skin_group(skin);
  std::vector<std::string> lines;
  for (const File& file : catalog.files) {
    if (file.group == group) lines.push_back(file.path + " " + std::to_string(file.size) + " " + file.sha256 + "\n");
  }
  std::ranges::sort(lines);
  std::string text;
  for (const std::string& line : lines) text += line;
  return text;
}

std::vector<std::string> picker_emoji(const Catalog& catalog) {
  constexpr std::string_view folder = "emoji/", suffix = ".svg";
  std::vector<std::string> unicode, made;
  for (const File& file : catalog.files) {
    if (!file.pick || file.group != "shared" || !file.path.starts_with(folder) || !file.path.ends_with(suffix)) continue;
    std::string id = file.path.substr(folder.size(), file.path.size() - folder.size() - suffix.size());
    if (id.find('/') != std::string::npos) continue;
    const bool hand_made = id.size() == 2 && std::ranges::all_of(id, [](char c) { return c >= '0' && c <= '9'; });
    (hand_made ? made : unicode).push_back(std::move(id));
  }
  std::ranges::sort(unicode);
  std::ranges::sort(made);
  unicode.insert(unicode.end(), made.begin(), made.end());
  return unicode;
}

std::string sound_group(std::string_view id) { return "sound:" + std::string(id); }

std::vector<std::string> sound_kinds(const Catalog& catalog, std::string_view id) {
  std::vector<std::string> kinds;
  for (const std::string_view kind : sound_kind_names) {
    const std::string folder = "sounds/" + std::string(kind) + "/" + std::string(id) + "/";
    if (std::ranges::any_of(catalog.files, [&](const File& file) { return file.path.starts_with(folder); })) kinds.emplace_back(kind);
  }
  return kinds;
}

std::vector<std::string> sound_files(const Catalog& catalog) {
  constexpr std::string_view folder = "sounds/";
  std::vector<std::string> paths;
  for (const File& file : catalog.files) {
    if (file.path.starts_with(folder)) paths.push_back(file.path.substr(folder.size()));
  }
  return paths;
}

}  // namespace crpp::assets
