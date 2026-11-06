#pragma once
// store::Platform on Windows: DPAPI bound to the current user, and file
// replacement that survives a power loss at any point.
#include "crpp/store/store.hpp"

namespace crpp::store {

class WindowsPlatform final : public Platform {
 public:
  [[nodiscard]] std::expected<std::string, Error> protect(std::string_view plain) override;
  [[nodiscard]] std::expected<std::string, Error> unprotect(std::string_view blob) override;
  [[nodiscard]] std::expected<void, Error> replace_file(const std::filesystem::path& path,
                                                        std::string_view bytes) override;
};

// %LOCALAPPDATA%\crpp and %LOCALAPPDATA%\Cloudroom.
[[nodiscard]] std::expected<Locations, Error> default_locations();

}  // namespace crpp::store
