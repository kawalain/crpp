#pragma once
// Fetches over WinHTTP, with a request of its own for each call on one shared
// session, and hashes with BCrypt.
#include <memory>

#include "crpp/assets/assets.hpp"

namespace crpp::assets {

class WindowsPlatform final : public Platform {
 public:
  // base_url ends with a slash; entry paths are appended to it. They hold only
  // characters that need no escaping, which the manifest's generator enforces.
  explicit WindowsPlatform(std::string_view base_url);
  ~WindowsPlatform();
  WindowsPlatform(const WindowsPlatform&) = delete;
  WindowsPlatform& operator=(const WindowsPlatform&) = delete;

  std::expected<std::vector<std::uint8_t>, Error> fetch(const Entry& entry, const OnBytes& on_bytes) override;
  std::string sha256(std::span<const std::uint8_t> bytes) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace crpp::assets
