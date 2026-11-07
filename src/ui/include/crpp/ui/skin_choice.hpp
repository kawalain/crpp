#pragma once
// Which Skin, Theme and dark mode the user has, against the packs there are. What it reads of a
// Theme comes from its pack, so a new pack or Theme needs nothing here.
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "crpp/skin/pack.hpp"
#include "crpp/ui/prefs.hpp"

namespace crpp::ui {

// What a pack offers, kept when the pack itself is not the one in use.
struct SkinInfo {
  std::string id, dir;  // dir: its folder, in UTF-8
  std::vector<std::pair<std::string, std::string>> names;  // by locale id
  std::vector<skin::PackTheme> themes;
  std::string default_theme;
};

[[nodiscard]] SkinInfo skin_info(const skin::Pack& pack, std::string dir);

// The name for this locale, or else the first it has.
[[nodiscard]] std::string_view name_in(std::span<const std::pair<std::string, std::string>> names, std::string_view locale);

enum class DarkMode { Off, On, System };

struct SkinChoice {
  const SkinInfo* skin = nullptr;
  const skin::PackTheme* theme = nullptr;
  DarkMode dark = DarkMode::Off;
  // The Palette to draw with, given what the system prefers.
  [[nodiscard]] std::string_view palette(bool system_dark) const;
  [[nodiscard]] bool has_dark() const { return theme && !theme->dark.empty(); }
};

// From this client's own keys, or, while they are absent, from upstream's, which are only read.
// What names no pack or Theme goes to the first pack and that pack's default Theme. Empty when there are no packs.
[[nodiscard]] SkinChoice choice_of(const Prefs& prefs, std::span<const SkinInfo> skins);

}  // namespace crpp::ui
