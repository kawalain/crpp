#pragma once
// WinHTTP, BCrypt and Win32 files behind update::Platform.
#include <memory>

#include "crpp/update/update.hpp"

namespace crpp::update {

class WindowsPlatform final : public Platform {
 public:
  // relaunch_args are appended to the command line of the exe launch() starts. A new
  // exe gets scanned as it starts, which on a slow machine takes a while.
  explicit WindowsPlatform(std::wstring relaunch_args = {}, unsigned long launch_wait_ms = 30000);
  ~WindowsPlatform();
  WindowsPlatform(const WindowsPlatform&) = delete;
  WindowsPlatform& operator=(const WindowsPlatform&) = delete;

  std::expected<std::vector<std::uint8_t>, Error> get(const std::string& url, std::size_t limit, std::string_view accept,
                                                      const OnBytes& on_bytes) override;
  std::string sha256(std::span<const std::uint8_t> bytes) override;
  bool verify(const PublicKey& key, std::span<const std::uint8_t> message, const Signature& signature) override;
  bool write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) override;
  std::expected<std::vector<std::uint8_t>, Error> read_file(const std::filesystem::path& path, std::size_t limit) override;
  bool move_file(const std::filesystem::path& from, const std::filesystem::path& to) override;
  bool remove_file(const std::filesystem::path& path) override;
  bool launch(const std::filesystem::path& exe) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// ECDSA P-256 over the SHA-256 of message, with BCrypt; also what checks a catalog.
[[nodiscard]] bool verify_signature(const PublicKey& key, std::span<const std::uint8_t> message, const Signature& signature);

// SHA-256 with BCrypt, in lowercase hex; empty if it could not be worked out.
[[nodiscard]] std::string sha256_hex(std::span<const std::uint8_t> bytes);

// Writes the file anew; the bytes are on the disk when this returns true.
[[nodiscard]] bool write_flushed(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);

// The path of this process's exe.
[[nodiscard]] std::filesystem::path running_exe();

// In the new exe, started by launch() with `--updated-from <pid>`: says it came up.
void report_started(unsigned long parent_pid);
// Then waits for the old process to leave, since only one instance may run. A while,
// not forever: the single-instance check takes over from there.
void wait_for_exit(unsigned long parent_pid);

}  // namespace crpp::update
