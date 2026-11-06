#pragma once
// What the build decides. Generated from src/update/CMakeLists.txt.
#include <optional>
#include <string_view>

#include "crpp/update/update.hpp"

namespace crpp::update {

// The version this exe was built as.
[[nodiscard]] std::string_view built_version() noexcept;
// A build made by the release workflow. Only these ask GitHub for anything.
[[nodiscard]] bool official_build() noexcept;
// The key releases are signed with; empty while the repository has only its
// placeholder, and then every update is refused.
[[nodiscard]] std::optional<PublicKey> built_in_key() noexcept;

}  // namespace crpp::update
