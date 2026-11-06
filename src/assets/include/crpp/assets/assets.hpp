#pragma once
// Keeps a folder of assets equal to a manifest built into the exe. Every file
// is checked against the manifest before it is kept, so whoever serves the files
// cannot change what the app loads.
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace crpp::assets {

struct Entry {
  std::string_view path;  // relative, with forward slashes
  std::uint64_t size = 0;
  std::string_view sha256;  // lowercase hex
  std::size_t source = 0;   // index into Manifest::sources
};

// Where a group of files is served from. The URL is `url` followed by the entry's path
// without `strip`.
struct Source {
  std::string_view url, strip;
};

struct Manifest {
  std::span<const Entry> entries;
  std::span<const Source> sources;  // none: every file is served under its own path
};

// Network: no answer from the server. Unavailable: it answered, without the file.
enum class Error : std::uint8_t { Network, Unavailable, BadContent, Disk };

// Called from several threads at once.
class Platform {
 public:
  // Told how many bytes of the file have arrived so far.
  using OnBytes = std::function<void(std::uint64_t received)>;
  // `remote` is the file's address under the platform's base URL. At most
  // entry.size + 1 bytes; more than that is not worth reading.
  virtual std::expected<std::vector<std::uint8_t>, Error> fetch(const Entry& entry, std::string_view remote,
                                                                const OnBytes& on_bytes) = 0;
  virtual std::string sha256(std::span<const std::uint8_t> bytes) = 0;

 protected:
  ~Platform() = default;
};

// The entries that need work. This only compares the list of what was installed
// and file sizes, so it is cheap enough for every start.
[[nodiscard]] std::vector<const Entry*> missing(const Manifest& manifest, const std::filesystem::path& dir);

// Whether an install has run here before, whatever came of it.
[[nodiscard]] bool tried(const std::filesystem::path& dir);

// A file's address under the base URL.
[[nodiscard]] std::string remote_path(const Manifest& manifest, const Entry& entry);

using OnProgress = std::function<void(std::uint64_t done, std::uint64_t total)>;

// Fetches what is missing, a few files at a time, and removes files older
// manifests installed. A file that fails does not stop the others, but no answer from
// the server or a full disk does, as the rest would only fail the same way. What arrived
// is kept and listed either way, and the first error is returned; calling again fetches
// the rest. progress is called in turn, from the working threads, and only moves forward.
std::expected<void, Error> install(const Manifest& manifest, const std::filesystem::path& dir, Platform& platform,
                                   const OnProgress& progress);

}  // namespace crpp::assets
