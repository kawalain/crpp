#pragma once
// Which Skin, Theme and dark mode the user has, against the packs there are. What it reads of a
// Theme comes from its pack, so a new pack or Theme needs nothing here.
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "crpp/skin/pack.hpp"
#include "crpp/ui/prefs.hpp"

namespace crpp::ui {

// What a pack offers, kept when the pack itself is not the one in use. One a Repository offers that
// is not installed has no folder, Themes with only their ids, names and whether they are dark, and
// how much it takes to fetch.
struct SkinInfo {
  std::string id, dir;  // dir: its folder, in UTF-8
  std::vector<std::pair<std::string, std::string>> names;  // by locale id
  std::vector<skin::PackTheme> themes;
  std::string default_theme;
  std::uint64_t to_fetch = 0;  // bytes, for one not installed
  std::string needs;           // for one not installed: why it cannot be picked yet, in words
};

// A Theme picked before its pack is installed: its pack is being fetched, or could not be.
struct ThemeFetch {
  enum class State : std::uint8_t { None, Fetching, Failed };
  State state = State::None;
  std::string skin, theme;  // the pack and Theme picked
  bool apply = true;        // still the last one picked, so it is applied once it is there
  int percent = 0;
  std::string reason;  // why it failed, in words
};

// How much a pack takes to fetch, as the first-run screen counts it: one decimal, at least 0.1.
[[nodiscard]] std::string megabytes(std::uint64_t bytes);

[[nodiscard]] SkinInfo skin_info(const skin::Pack& pack, std::string dir);

// The name for this locale, or else the English one, or else the id.
[[nodiscard]] std::string_view name_in(std::span<const std::pair<std::string, std::string>> names, std::string_view locale,
                                       std::string_view id = {});
// A Theme's name among these packs, as name_in gives it; empty when none of them has it.
[[nodiscard]] std::string_view theme_name(std::span<const SkinInfo> skins, std::string_view skin, std::string_view theme, std::string_view locale);

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

// Upstream's keys (theme, lightTheme, darkTheme) hold upstream's names, and one of them is a name
// of its own here: upstream's "cloudroom" is "cumulus". Every other id stays as it is.
[[nodiscard]] std::string_view theme_from_upstream(std::string_view id);

}  // namespace crpp::ui
