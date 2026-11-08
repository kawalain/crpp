#pragma once
// A Repository's list of files (ADR 0008): every file the program downloads, with its size
// and hash, the Skin Packs it offers, and other addresses that serve the same files. It is
// signed, and nothing it names is used before the signature checks out.
//
// Text, LF only, one item per line, fields split by single spaces:
//   crpp-catalog 1
//   key <128 lowercase hex>                    the P-256 key that signs it
//   source <name> <url>                        another address for files; ends with '/'
//   skin <id> <format> [<parent>]
//   default-skin <id>
//   file <group> <path> <size> <sha256> [from <source> <remote>] [nopick]
//   signature <128 lowercase hex>              covers every byte before its line
// The first two lines come first, the signature last; the others in any order, a source
// before the files that name it. A group is `shared`, `skin:<id>` or `sound:<id>`. nopick
// keeps an emoji out of the picker. Paths need no escaping in a URL.
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "crpp/update/update.hpp"

namespace crpp::assets {

inline constexpr std::string_view catalog_name = "catalog.txt";
inline constexpr std::size_t max_catalog_size = 4 * 1024 * 1024;
// Cloudflare serves files up to 25 MiB; this leaves room for another host.
inline constexpr std::uint64_t max_file_size = 64 * 1024 * 1024;

struct File {
  std::string group, path;  // the path is where it is installed, relative to the assets folder
  std::uint64_t size = 0;
  std::string sha256;  // lowercase hex
  std::string source;  // a source's name, or empty: only the Repository has it
  std::string remote;  // its path under that source
  bool pick = true;
};

struct SourceUrl {
  std::string name, url;
};

struct SkinPack {
  std::string id;
  int format = 0;
  std::string parent;  // empty: it extends none
};

struct Catalog {
  update::PublicKey key{};
  std::vector<SourceUrl> sources;
  std::vector<SkinPack> skins;
  std::string default_skin;
  std::vector<File> files;
  std::optional<update::Signature> signature;
  std::size_t signed_bytes = 0;  // how much of the text the signature covers

  [[nodiscard]] const SourceUrl* source(std::string_view name) const;
  [[nodiscard]] const SkinPack* skin(std::string_view id) const;
};

// The line and what is wrong with it, for the diagnostic log and the tool.
struct CatalogError {
  std::size_t line = 0;  // 1-based; 0 for the whole text
  std::string what;
};

// Checks the shape of everything, not the signature: an unsigned catalog parses too.
[[nodiscard]] std::expected<Catalog, CatalogError> parse_catalog(std::string_view text);

// Every line but the signature, which the signing script appends.
[[nodiscard]] std::string write_catalog(const Catalog& catalog);

// The skin and every pack it extends, nearest first. Empty for one the catalog lacks.
[[nodiscard]] std::vector<std::string> skin_chain(const Catalog& catalog, std::string_view id);

[[nodiscard]] std::string skin_group(std::string_view id);

// The emoji the picker offers, in order: the Unicode ones, then the hand-made ones,
// whose names are two digits.
[[nodiscard]] std::vector<std::string> picker_emoji(const Catalog& catalog);

// The Sound pack files, relative to the sounds folder.
[[nodiscard]] std::vector<std::string> sound_files(const Catalog& catalog);

}  // namespace crpp::assets
