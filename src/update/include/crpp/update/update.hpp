#pragma once
// Finds a newer release, proves it is ours and swaps it in for the running exe.
//
// GitHub is only a mirror: nothing it says about a release is trusted until the
// manifest's signature checks out against the key built into the app. The manifest
// names the version and the hash of the exe, so an old release cannot be replayed as
// new and the exe cannot be changed. Every failure leaves the running app as it was.
#include <array>
#include <compare>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace crpp::update {

// NoKey: this build has no real public key, so it refuses every update.
// RollbackFailed: the swap failed and the old exe could not be put back. The running
// process is unaffected, but `<exe>.old` must be renamed by hand.
enum class Error : std::uint8_t { Network, Unavailable, BadContent, BadSignature, NoKey, Disk, Launch, RollbackFailed };

// Short enough for the failure toast.
[[nodiscard]] std::string_view error_name(Error error) noexcept;

struct Version {
  std::uint32_t major = 0, minor = 0, patch = 0;
  friend auto operator<=>(const Version&, const Version&) = default;
};

// MAJOR.MINOR.PATCH with an optional leading 'v'. No suffixes: a build that is not a
// plain release has no place in the order.
[[nodiscard]] std::optional<Version> parse_version(std::string_view text) noexcept;
[[nodiscard]] std::string to_string(Version version);

using PublicKey = std::array<std::uint8_t, 64>;  // X || Y, big-endian, on P-256
using Signature = std::array<std::uint8_t, 64>;  // r || s, big-endian

[[nodiscard]] std::optional<PublicKey> parse_public_key(std::string_view hex) noexcept;

inline constexpr std::string_view manifest_asset = "crpp-update.manifest";
inline constexpr std::uint64_t max_exe_size = 128 * 1024 * 1024;

// Six lines, LF only, in this order. The signature covers every byte before its line.
//   crpp-update 1
//   version 0.3.1
//   asset crpp.exe
//   size 7340032
//   sha256 <64 lowercase hex>
//   signature <128 lowercase hex>
struct Manifest {
  Version version;
  std::string asset;
  std::uint64_t size = 0;
  std::string sha256;
  Signature signature{};
  std::size_t signed_bytes = 0;  // how much of the file the signature covers
};
[[nodiscard]] std::expected<Manifest, Error> parse_manifest(std::span<const std::uint8_t> bytes);

// A published release; the tag is always "v" + version.
struct Release {
  Version version;
  std::string name, body, published_at;
  [[nodiscard]] std::string tag() const { return "v" + to_string(version); }
};

// From GitHub's list of releases or from dump_releases, newest version first. Drafts,
// prereleases and tags that are not plain versions are left out.
[[nodiscard]] std::expected<std::vector<Release>, Error> parse_releases(std::string_view json);
// What the changelog needs, for the cache.
[[nodiscard]] std::string dump_releases(std::span<const Release> releases);

// "2026-11-04T09:00:00Z" as upstream's changelog writes dates, or empty.
[[nodiscard]] std::string korean_date(std::string_view published_at);

class Platform {
 public:
  using OnBytes = std::function<void(std::uint64_t received)>;
  // HTTPS GET following redirects. At most limit + 1 bytes, so a longer reply shows.
  // `accept` is the Accept header; empty sends none.
  virtual std::expected<std::vector<std::uint8_t>, Error> get(const std::string& url, std::size_t limit,
                                                              std::string_view accept, const OnBytes& on_bytes) = 0;
  virtual std::string sha256(std::span<const std::uint8_t> bytes) = 0;
  // ECDSA P-256 over the SHA-256 of message.
  virtual bool verify(const PublicKey& key, std::span<const std::uint8_t> message, const Signature& signature) = 0;

  // The bytes are on disk when this returns.
  virtual bool write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) = 0;
  virtual std::expected<std::vector<std::uint8_t>, Error> read_file(const std::filesystem::path& path, std::size_t limit) = 0;
  // Replaces `to` if it exists. Must work on the exe of a running process.
  virtual bool move_file(const std::filesystem::path& from, const std::filesystem::path& to) = 0;
  // True if the file is gone, also if it never existed.
  virtual bool remove_file(const std::filesystem::path& path) = 0;
  // Starts the exe and returns true once it reports that it came up; on any other
  // outcome no child is left running.
  virtual bool launch(const std::filesystem::path& exe) = 0;

 protected:
  ~Platform() = default;
};

struct Source {
  std::string releases_url;   // lists the releases as JSON
  std::string download_base;  // ends with a slash; <tag>/<asset> is appended
};
// This repository on GitHub.
[[nodiscard]] Source published_source();

struct Available {
  Release release;
  Manifest manifest;
};

struct CheckResult {
  std::vector<Release> releases;    // for the changelog
  std::optional<Available> update;  // verified and newer than the running version
  // A newer release exists but could not be proved; update is empty.
  std::optional<Error> problem;
};

// Asks for the releases and, if one is newer, fetches and verifies its manifest.
// Without a key this fails before anything goes out.
[[nodiscard]] std::expected<CheckResult, Error> check(const Source& source, Version current,
                                                      const std::optional<PublicKey>& key, Platform& platform);

// The exe of a verified release, checked against its manifest.
[[nodiscard]] std::expected<std::vector<std::uint8_t>, Error> download(const Source& source, const Available& available,
                                                                       Platform& platform, const Platform::OnBytes& on_bytes);

// The apply step, and the only one an installer would replace: writes the new exe
// beside the running one, swaps the names, starts it, and undoes all of it if the new
// exe does not come up. On success the caller exits; the new process removes the rest.
[[nodiscard]] std::expected<void, Error> self_replace(const std::filesystem::path& exe, std::span<const std::uint8_t> bytes,
                                                      std::string_view sha256, Platform& platform);

// What an earlier replacement left beside the exe. Failures are harmless: the next
// start tries again.
void remove_leftovers(const std::filesystem::path& exe, Platform& platform);

// A version's notes open once: when it is first noticed, or else when it first runs after
// an update. `shown` is the version they last opened for, `seen` the version last started;
// both are empty if there was none.
[[nodiscard]] bool notes_due_when_noticed(std::string_view shown, std::string_view available) noexcept;
[[nodiscard]] bool notes_due_at_start(std::string_view seen, std::string_view shown, std::string_view current) noexcept;

}  // namespace crpp::update
