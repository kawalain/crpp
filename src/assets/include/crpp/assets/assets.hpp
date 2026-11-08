#pragma once
// Keeps a folder of assets equal to a Repository's signed catalog (ADR 0008). Every file
// is checked against the catalog before it is kept, so whoever serves the files cannot
// change what the app loads.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "crpp/assets/catalog.hpp"

namespace crpp::assets {

// Network: no answer from the server. Unavailable: it answered, without the file.
// BadContent: a file or catalog other than the one expected. BadSignature: a catalog
// not signed by the Repository's key.
enum class Error : std::uint8_t { Network, Unavailable, BadContent, BadSignature, Disk };

// Called from several threads at once.
class Platform {
 public:
  // Told how many bytes of the file have arrived so far.
  using OnBytes = std::function<void(std::uint64_t received)>;
  // A GET of the address, without following redirects. At most limit + 1 bytes; more
  // than that is not worth reading.
  virtual std::expected<std::vector<std::uint8_t>, Error> fetch(const std::string& url, std::uint64_t limit,
                                                                const OnBytes& on_bytes) = 0;
  virtual std::string sha256(std::span<const std::uint8_t> bytes) = 0;
  // ECDSA P-256 over the SHA-256 of message.
  virtual bool verify(const update::PublicKey& key, std::span<const std::uint8_t> message,
                      const update::Signature& signature) = 0;
  // Before another attempt at a file the network did not deliver.
  virtual void wait(std::chrono::milliseconds time) = 0;

 protected:
  ~Platform() = default;
};

struct Repository {
  std::string url;     // the folder of its catalog and files; ends with '/'
  std::string mirror;  // another folder with the same catalog, or empty
  update::PublicKey key{};
};

struct Fetched {
  Catalog catalog;
  std::string text;          // as signed, to keep
  bool from_mirror = false;  // then the Repository itself did not answer
};

// The Repository's catalog, verified, from its own folder or else from its mirror.
[[nodiscard]] std::expected<Fetched, Error> fetch_catalog(const Repository& repository, Platform& platform);

// SHA-256 of the key's 64 bytes, in lowercase hex. A Repository other than the official one
// is known by its first 16 digits, which name its folder and come before its packs' ids.
[[nodiscard]] std::string fingerprint(const update::PublicKey& key, Platform& platform);
inline constexpr std::size_t short_fingerprint = 16;

// The SHA-256 of digest_text: what a pin's sha256 says of the pack it extends.
[[nodiscard]] std::string own_digest(const Catalog& catalog, std::string_view skin, Platform& platform);

// A Repository as a chain of packs sees it: the key it is known by, and its catalog, if one is read.
struct Shelf {
  std::string fingerprint;
  const Catalog* catalog = nullptr;
};

// How deep packs may extend each other, the pack itself included, as skin::max_pack_levels.
inline constexpr std::size_t max_chain = 4;

// A pack and those it extends, which may be in other Repositories. Pointers are into the catalogs.
struct Chain {
  struct Link {
    std::size_t shelf = 0;
    std::string pack;
    const ParentPin* pin = nullptr;  // what the pack before it says of this one, if anything
  };
  enum class Gap : std::uint8_t {
    None,
    Missing,     // a catalog read does not offer the pack, or none is read
    Repository,  // `wanted` names a Repository that is not added
    TooDeep      // more than max_chain packs, or one that comes round again
  };
  std::vector<Link> links;  // the pack first; complete only without a gap
  Gap gap = Gap::None;
  const ParentPin* wanted = nullptr;
};

// Follows a pack of shelves[start] to what it extends: in the same catalog by its skin line, in
// the Repository its pin's fingerprint names, or else in the official one (npos: none).
[[nodiscard]] Chain pack_chain(std::span<const Shelf> shelves, std::size_t official, std::size_t start, std::string_view pack);

// Whether the pack in that catalog is still what a pin says: its version and its own digest.
// Both are checked; another digest is the graver, as the content is not what was pinned.
enum class PinCheck : std::uint8_t { Holds, Version, Digest };
[[nodiscard]] PinCheck check_pin(const ParentPin& pin, const Catalog& catalog, std::string_view pack, Platform& platform);

// The catalog in a folder no key is known for yet, signed by the key it states. Nothing vouches
// for that key: the user is shown its fingerprint before it is trusted. One that offers more than
// packs (offers_only_packs) is BadContent.
[[nodiscard]] std::expected<Fetched, Error> fetch_new_catalog(const std::string& folder, Platform& platform);

// The catalog kept in `dir`, if it is there and signed by `key`.
[[nodiscard]] std::optional<Catalog> load_catalog(const std::filesystem::path& dir, const update::PublicKey& key,
                                                  Platform& platform);
// Without a signature check: for the folder beside a development exe, as trusted as the exe.
[[nodiscard]] std::optional<Catalog> load_local_catalog(const std::filesystem::path& dir);
// Replaces the kept one, unless it is the same; only a verified catalog belongs there.
[[nodiscard]] bool save_catalog(const std::filesystem::path& dir, std::string_view text);

// The groups kept current: shared files, every Sound pack, the default Skin with the
// packs it extends, and kept_groups.
[[nodiscard]] std::vector<std::string> wanted_groups(const Catalog& catalog, const std::filesystem::path& dir);
// Every Skin and Sound pack that has a file installed, with the packs a Skin extends: what a
// Repository the user added keeps current.
[[nodiscard]] std::vector<std::string> kept_groups(const Catalog& catalog, const std::filesystem::path& dir);

// The files of those groups that need work. This only compares the list of what was
// installed and file sizes, so it is cheap enough for every start.
[[nodiscard]] std::vector<const File*> missing(const Catalog& catalog, std::span<const std::string> groups,
                                               const std::filesystem::path& dir);

struct Progress {
  std::size_t files_done = 0, files = 0;
  std::uint64_t bytes_done = 0, bytes = 0;
};
using OnProgress = std::function<void(const Progress&)>;

// Fetches what is missing of the groups, a few files at a time, and removes files the
// catalog no longer lists. Each file comes from `base`, the Repository's folder, or else
// from its source; an empty base skips straight to the sources, as when the Repository did
// not answer for its catalog. A file the network did not deliver is tried twice more after
// a short wait, and once the Repository has failed that way the rest go to their sources.
// A file that fails does not stop the others, but no answer from anywhere or a full disk
// does, as the rest would only fail the same way. What arrived is kept and listed either
// way, and the first error is returned; calling again fetches the rest. progress is called
// in turn, from the working threads, and only moves forward. Once `cancel` is set no file is
// begun, and what arrived is kept as after a failure. A path in `held` is open and cannot be
// replaced now, or belongs to a pack held back: it is left as it is, listed with the hash it had,
// so it is missing at the next start, and is not removed when the catalog no longer lists it. One
// ending with '/' holds everything under it.
std::expected<void, Error> install(const Catalog& catalog, std::string_view base, std::span<const std::string> groups,
                                   const std::filesystem::path& dir, Platform& platform, const OnProgress& progress,
                                   const std::atomic<bool>* cancel = nullptr, std::span<const std::string> held = {});

// Removes what is installed of these groups: the files the catalog lists for them, and for a
// Skin's group whatever is listed under its folder, skins/<id>/, which an older catalog may have
// put there. Folders left empty go too. A file that cannot be removed only takes up space, and is
// no longer listed.
std::expected<void, Error> uninstall(const Catalog& catalog, std::span<const std::string> groups, const std::filesystem::path& dir);

}  // namespace crpp::assets
