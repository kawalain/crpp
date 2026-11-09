#pragma once
// A Repository's list of files (ADR 0008): every file the program downloads, with its size
// and hash, the Skin Packs it offers, and other addresses that serve the same files. It is
// signed, and nothing it names is used before the signature checks out. A Repository serves
// each file at <folder>/<sha256> (ADR 0010); the path is only what the program calls it.
//
// Text, LF only, one item per line, fields split by single spaces:
//   crpp-catalog 2                             1 is the grammar before ADR 0010, read only from disk
//   key <128 lowercase hex>                    the P-256 key that signs it
//   mirror <url>                               another folder with the same files; ends with '/'
//   source <name> <url>                        where a third-party file comes from; ends with '/'
//   skin <id> <format> [<parent>]              a parent here is one of this catalog
//   skin-version <skin> <0.y.z>
//   parent <skin> <id> [fingerprint <hex>] [repository <address>] [version <0.y[.z]>] [sha256 <hex>]
//                                              the pack it extends, when it is in another catalog or pinned
//   skin-name <skin> <locale> <words>          what a user calls it, for Themes not installed
//   theme <skin> <id> [dark]                   a Theme it offers, and whether it has a dark variant
//   theme-name <skin> <theme> <locale> <words>
//   default-skin <id>
//   sound <id>                                 a Sound pack, of one or more kinds
//   sound-name <sound> <locale> <words>        what a user calls it, for packs not installed
//   file <group> <path> <size> <sha256> [from <source> <remote>] [nopick]
//   signature <128 lowercase hex>              covers every byte before its line
// The first two lines come first, the signature last; the others in any order, a source
// before the files that name it, a skin before its names and Themes and a Theme before its
// names. Words run to the end of the line. A group is `shared`, `skin:<id>` or `sound:<id>`.
// nopick keeps an emoji out of the picker. Paths need no escaping in a URL. A line of a kind
// not listed here is skipped, so an older exe reads a newer catalog; one of a kind listed
// here that is wrong refuses the catalog.
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

using Names = std::vector<std::pair<std::string, std::string>>;  // by locale id

struct CatalogTheme {
  std::string id;
  bool dark = false;
  Names names;
};

// What a pack says of the pack it extends: with no fingerprint, one of the same catalog or else of
// the official Repository's; with one, of the Repository of that key.
struct ParentPin {
  std::string id;
  std::string fingerprint;  // 64 lowercase hex, or empty
  std::string repository;   // the address to add that Repository by, or empty
  std::string version;      // 0.y or 0.y.z, or empty
  std::string sha256;       // own_digest of it, or empty
};

// A Sound pack: its files are sounds/<kind>/<id>/ for each kind it has, keyboard, mouse or pen.
struct SoundPack {
  std::string id;
  Names names;
};

struct SkinPack {
  std::string id;
  int format = 0;
  std::string parent;  // one of this catalog it extends; empty: none here
  std::string version;
  std::optional<ParentPin> pin;  // set when it extends one elsewhere, or pins the one here
  Names names;
  std::vector<CatalogTheme> themes;

  [[nodiscard]] const CatalogTheme* theme(std::string_view theme_id) const;
};

// A pack's or Theme's id, and a locale's: a-z, 0-9, - and _, at most 32 characters.
[[nodiscard]] bool is_id(std::string_view s);

inline constexpr int catalog_grammar = 2;

struct Catalog {
  int grammar = catalog_grammar;  // 1 for one kept by an exe before ADR 0010
  update::PublicKey key{};
  std::string mirror;  // empty: none
  std::vector<SourceUrl> sources;
  std::vector<SkinPack> skins;
  std::string default_skin;
  std::vector<SoundPack> sounds;
  std::vector<File> files;
  std::optional<update::Signature> signature;
  std::size_t signed_bytes = 0;  // how much of the text the signature covers

  [[nodiscard]] const SourceUrl* source(std::string_view name) const;
  [[nodiscard]] const SkinPack* skin(std::string_view id) const;
  [[nodiscard]] const SoundPack* sound(std::string_view id) const;
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

// Whether a version holds to a pin: "0.3" takes any 0.3.z, "0.3.2" only that, and an empty pin any.
// A pack with no version holds to none but the empty one.
[[nodiscard]] bool version_matches(std::string_view pin, std::string_view version);

// What own_digest hashes: a line "<path> <size> <sha256>" per file of the pack's own group, sorted
// by path as bytes, each ending with LF.
[[nodiscard]] std::string digest_text(const Catalog& catalog, std::string_view skin);

// The folder of a Repository's catalog, from what a user typed: an HTTPS folder or the
// address of its catalog, or github:owner/repo[@ref], whose files raw.githubusercontent.com
// serves at the ref, HEAD when none is given. Plain HTTP only to this machine, for tests.
// Empty when it is none of these.
[[nodiscard]] std::string folder_of(std::string_view address);

// What a Repository other than the official one may offer: Skin and Sound packs, each in a
// folder of its own, skins/<id>/ or sounds/<id>/. Emoji, words and fonts stay the official
// one's, so the members of a Room see the same.
[[nodiscard]] bool offers_only_packs(const Catalog& catalog);


// The emoji the picker offers, in order: the Unicode ones, then the hand-made ones,
// whose names are two digits.
[[nodiscard]] std::vector<std::string> picker_emoji(const Catalog& catalog);

// The Sound pack files, relative to the sounds folder.
[[nodiscard]] std::vector<std::string> sound_files(const Catalog& catalog);

// The group of a Sound pack's files.
[[nodiscard]] std::string sound_group(std::string_view id);
// The kinds a Sound pack has files of: "keyboard", "mouse" or "pen", in that order.
[[nodiscard]] std::vector<std::string> sound_kinds(const Catalog& catalog, std::string_view id);

}  // namespace crpp::assets
