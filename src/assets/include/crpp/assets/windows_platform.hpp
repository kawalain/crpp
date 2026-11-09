#pragma once
// Fetches over WinHTTP, with a request of its own for each call on one shared session,
// hashes with BCrypt and verifies with the update module's BCrypt check.
#include <memory>

#include "crpp/assets/assets.hpp"

namespace crpp::assets {

class WindowsPlatform final : public Platform {
 public:
  // Addresses hold only characters that need no escaping, which the catalog enforces.
  WindowsPlatform();
  ~WindowsPlatform();
  WindowsPlatform(const WindowsPlatform&) = delete;
  WindowsPlatform& operator=(const WindowsPlatform&) = delete;

  std::expected<std::vector<std::uint8_t>, Error> fetch(const std::string& url, std::uint64_t limit, const OnBytes& on_bytes) override;
  std::string sha256(std::span<const std::uint8_t> bytes) override;
  bool verify(const update::PublicKey& key, std::span<const std::uint8_t> message, const update::Signature& signature) override;
  void wait(std::chrono::milliseconds time) override;
  bool write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crpp::assets
