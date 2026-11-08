#pragma once
// The Themes there are, in the order the settings list them. A Theme is one Skin with one
// Palette, so a new Palette is one more entry here and its colours in theme.hpp.
#include <span>
#include <string_view>
#include <vector>

#include "crpp/locale/locale.hpp"
#include "crpp/ui/theme.hpp"

namespace crpp::ui {

struct ThemeEntry {
  std::string_view id;  // what the settings keep
  const char* locale::Strings::*label;
  bool dark;  // which list it joins when following the system
  Theme theme;
};

[[nodiscard]] std::span<const ThemeEntry> all_themes();
[[nodiscard]] std::vector<const ThemeEntry*> all_theme_entries();
// The light or the dark ones; the first of each is what an unknown id means there.
[[nodiscard]] std::vector<const ThemeEntry*> themes_of(bool dark);
// An id it does not know is the default Theme, as upstream takes an unknown name.
[[nodiscard]] const ThemeEntry& theme_named(std::string_view id);

// Upstream's keys (theme, lightTheme, darkTheme) hold upstream's names, and one of them is a
// name of its own here: upstream's "cloudroom" is "cumulus". These turn one into the other,
// and leave every other id as it is.
[[nodiscard]] std::string_view theme_from_upstream(std::string_view id);
[[nodiscard]] std::string_view theme_to_upstream(std::string_view id);

}  // namespace crpp::ui
