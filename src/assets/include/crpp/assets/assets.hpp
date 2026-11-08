#pragma once
// Keeps a folder of assets equal to a Repository's signed catalog (ADR 0008). Every file
// is checked against the catalog before it is kept, so whoever serves the files cannot
// change what the app loads.
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

// The catalog kept in `dir`, if it is there and signed by `key`.
[[nodiscard]] std::optional<Catalog> load_catalog(const std::filesystem::path& dir, const update::PublicKey& key,
                                                  Platform& platform);
// Without a signature check: for the folder beside a development exe, as trusted as the exe.
[[nodiscard]] std::optional<Catalog> load_local_catalog(const std::filesystem::path& dir);
// Replaces the kept one, unless it is the same; only a verified catalog belongs there.
[[nodiscard]] bool save_catalog(const std::filesystem::path& dir, std::string_view text);

// The groups kept current: shared files, every Sound pack, the default Skin with the
// packs it extends, and every Skin that has a file installed.
[[nodiscard]] std::vector<std::string> wanted_groups(const Catalog& catalog, const std::filesystem::path& dir);

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
// in turn, from the working threads, and only moves forward.
std::expected<void, Error> install(const Catalog& catalog, std::string_view base, std::span<const std::string> groups,
                                   const std::filesystem::path& dir, Platform& platform, const OnProgress& progress);

}  // namespace crpp::assets
