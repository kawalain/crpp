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
};

struct Manifest {
  std::string_view commit;  // of this repository; empty when built outside git
  std::span<const Entry> entries;
};

// Network: no answer from the server. Unavailable: it answered, without the file.
enum class Error : std::uint8_t { Network, Unavailable, BadContent, Disk };

class Platform {
 public:
  // Told how many bytes of the file have arrived so far.
  using OnBytes = std::function<void(std::uint64_t received)>;
  // At most entry.size + 1 bytes; more than that is not worth reading.
  virtual std::expected<std::vector<std::uint8_t>, Error> fetch(const Entry& entry, const OnBytes& on_bytes) = 0;
  virtual std::string sha256(std::span<const std::uint8_t> bytes) = 0;

 protected:
  ~Platform() = default;
};

// The entries that need work. This only compares the list of what was installed
// and file sizes, so it is cheap enough for every start.
[[nodiscard]] std::vector<const Entry*> missing(const Manifest& manifest, const std::filesystem::path& dir);

using OnProgress = std::function<void(std::uint64_t done, std::uint64_t total)>;

// Fetches what is missing and removes files older manifests installed. After a
// failure, calling again keeps what already arrived.
std::expected<void, Error> install(const Manifest& manifest, const std::filesystem::path& dir, Platform& platform,
                                   const OnProgress& progress);

}  // namespace crpp::assets
