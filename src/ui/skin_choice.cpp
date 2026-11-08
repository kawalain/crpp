#include "crpp/ui/skin_choice.hpp"

#include <algorithm>

#include "crpp/ui/themes.hpp"

namespace crpp::ui {

namespace {

const skin::PackTheme* theme_in(const SkinInfo& skin, std::string_view id) {
  const auto found = std::ranges::find(skin.themes, id, &skin::PackTheme::id);
  return found == skin.themes.end() ? nullptr : &*found;
}

const skin::PackTheme* default_of(const SkinInfo& skin) {
  if (const skin::PackTheme* named = theme_in(skin, skin.default_theme)) return named;
  return skin.themes.empty() ? nullptr : &skin.themes.front();
}

// Upstream keeps one id, of a light or a dark list. The Theme that has it as a Palette is the one meant.
SkinChoice from_upstream(const Prefs& prefs, std::span<const SkinInfo> skins) {
  const bool follows = prefs.get(pref::system_theme);
  const std::string saved = prefs.get(follows ? pref::light_theme : pref::theme);
  const std::string_view id = theme_from_upstream(saved);
  for (const SkinInfo& skin : skins) {
    for (const skin::PackTheme& theme : skin.themes) {
      if (theme.palette != id && theme.dark != id) continue;
      const DarkMode dark = follows ? DarkMode::System : theme.dark == id ? DarkMode::On : DarkMode::Off;
      return {&skin, &theme, dark};
    }
  }
  return {&skins.front(), default_of(skins.front()), follows ? DarkMode::System : DarkMode::Off};
}

}  // namespace

SkinInfo skin_info(const skin::Pack& pack, std::string dir) { return {pack.id, std::move(dir), pack.names, pack.themes, pack.default_theme}; }

std::string_view name_in(std::span<const std::pair<std::string, std::string>> names, std::string_view locale, std::string_view id) {
  for (const std::string_view wanted : {locale, std::string_view("en")}) {
    for (const auto& [in, name] : names) {
      if (in == wanted) return name;
    }
  }
  if (!id.empty() || names.empty()) return id;
  return names.front().second;
}

std::string_view theme_name(std::span<const SkinInfo> skins, std::string_view skin, std::string_view theme, std::string_view locale) {
  for (const SkinInfo& info : skins) {
    if (info.id != skin) continue;
    for (const skin::PackTheme& one : info.themes) {
      if (one.id == theme) return name_in(one.names, locale, one.id);
    }
  }
  return {};
}

std::string_view SkinChoice::palette(bool system_dark) const {
  if (!theme) return {};
  const bool wants_dark = dark == DarkMode::On || (dark == DarkMode::System && system_dark);
  return wants_dark && has_dark() ? theme->dark : theme->palette;
}

SkinChoice choice_of(const Prefs& prefs, std::span<const SkinInfo> skins) {
  if (skins.empty()) return {};
  const std::string skin_id = prefs.get(pref::skin);
  if (skin_id.empty()) return from_upstream(prefs, skins);
  const auto found = std::ranges::find(skins, std::string_view(skin_id), &SkinInfo::id);
  const SkinInfo& skin = found == skins.end() ? skins.front() : *found;
  // An earlier build kept upstream's name for 뭉게구름 here too.
  const skin::PackTheme* theme = theme_in(skin, theme_from_upstream(prefs.get(pref::skin_theme)));
  const std::string dark = prefs.get(pref::dark_mode);
  return {&skin, theme ? theme : default_of(skin), dark == "on" ? DarkMode::On : dark == "system" ? DarkMode::System : DarkMode::Off};
}

}  // namespace crpp::ui
