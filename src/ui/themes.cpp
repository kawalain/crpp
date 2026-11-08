#include "crpp/ui/themes.hpp"

#include <algorithm>
#include <array>

namespace crpp::ui {

namespace {

constexpr std::string_view upstream_cumulus = "cloudroom";

// THEMES, LIGHT_THEMES and DARK_THEMES, in upstream's order and with its names but the first.
const std::array<ThemeEntry, 5> themes{{
    {"cumulus", &locale::Strings::theme_cumulus, false, light_cloud_palette(rgb(0x66ccff))},
    {"darkcloud", &locale::Strings::theme_dark_cloud, true, dark_cloud_palette()},
    {"pastel", &locale::Strings::theme_pastel, false, light_cloud_palette(rgb(0xb1dafd))},
    {"luna_blue", &locale::Strings::theme_luna_blue, false, luna_theme(luna_blue_colors(), false)},
    {"luna_blue_dark", &locale::Strings::theme_luna_blue_dark, true, luna_theme(luna_blue_dark_colors(), true)},
}};

}  // namespace

std::span<const ThemeEntry> all_themes() { return themes; }

std::vector<const ThemeEntry*> all_theme_entries() {
  std::vector<const ThemeEntry*> list;
  for (const ThemeEntry& entry : themes) list.push_back(&entry);
  return list;
}

std::vector<const ThemeEntry*> themes_of(bool dark) {
  std::vector<const ThemeEntry*> list;
  for (const ThemeEntry& entry : themes) {
    if (entry.dark == dark) list.push_back(&entry);
  }
  return list;
}

const ThemeEntry& theme_named(std::string_view id) {
  const auto found = std::ranges::find(themes, id, &ThemeEntry::id);
  return found == themes.end() ? themes.front() : *found;
}

std::string_view theme_from_upstream(std::string_view id) { return id == upstream_cumulus ? themes.front().id : id; }

std::string_view theme_to_upstream(std::string_view id) { return id == themes.front().id ? upstream_cumulus : id; }

}  // namespace crpp::ui
