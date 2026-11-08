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
    for (const auto& [locale, words] : skin.names) text += "skin-name " + skin.id + " " + locale + " " + words + "\n";
    for (const CatalogTheme& theme : skin.themes) {
      text += "theme " + skin.id + " " + theme.id + (theme.dark ? " dark" : "") + "\n";
      for (const auto& [locale, words] : theme.names) text += "theme-name " + skin.id + " " + theme.id + " " + locale + " " + words + "\n";
    }
  }
  if (!catalog.default_skin.empty()) text += "default-skin " + catalog.default_skin + "\n";
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

std::string skin_group(std::string_view id) { return "skin:" + std::string(id); }

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

std::vector<std::string> sound_files(const Catalog& catalog) {
  constexpr std::string_view folder = "sounds/";
  std::vector<std::string> paths;
  for (const File& file : catalog.files) {
    if (file.path.starts_with(folder)) paths.push_back(file.path.substr(folder.size()));
  }
  return paths;
}

}  // namespace crpp::assets
