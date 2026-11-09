#include "crpp/ui/skin_choice.hpp"

#include "check.hpp"

using namespace crpp::ui;
namespace skin = crpp::skin;

namespace {

skin::PackTheme theme(std::string id, std::string dark = {}) {
  skin::PackTheme t;
  t.id = id;
  t.palette = std::move(id);
  t.dark = std::move(dark);
  return t;
}

std::vector<SkinInfo> packs() {
  return {{"one", "dir/one", {{"ko", "하나"}, {"en", "One"}}, {theme("sky", "night"), theme("soft")}, "soft"}, {"two", "dir/two", {{"en", "Two"}}, {theme("old", "old_dark"), theme("new", "new_dark")}, "old"}};
}

void no_packs_choose_nothing() {
  MemoryPrefs prefs;
  const SkinChoice choice = choice_of(prefs, {});
  CHECK(choice.skin == nullptr && choice.theme == nullptr);
  CHECK(choice.palette(true).empty());
}

void upstreams_keys_say_it_until_our_own_are_there() {
  const auto skins = packs();
  MemoryPrefs prefs;
  prefs.set(pref::theme, "night");  // a dark variant is its Theme, with dark on
  SkinChoice c = choice_of(prefs, skins);
  CHECK(c.skin->id == "one" && c.theme->id == "sky" && c.dark == DarkMode::On);
  CHECK(c.palette(false) == "night");
  prefs.set(pref::theme, "new");
  c = choice_of(prefs, skins);
  CHECK(c.skin->id == "two" && c.theme->id == "new" && c.dark == DarkMode::Off);
  // Following the system goes by the light one.
  prefs.set(pref::system_theme, true);
  prefs.set(pref::light_theme, "soft");
  c = choice_of(prefs, skins);
  CHECK(c.theme->id == "soft" && c.dark == DarkMode::System);
  // What no pack knows goes to the first pack's default.
  prefs.set(pref::system_theme, false);
  prefs.set(pref::theme, "gone");
  c = choice_of(prefs, skins);
  CHECK(c.skin->id == "one" && c.theme->id == "soft" && c.dark == DarkMode::Off);
  // An empty id names nothing, not a Theme that has no dark variant.
  prefs.set(pref::theme, "");
  c = choice_of(prefs, skins);
  CHECK(c.skin->id == "one" && c.theme->id == "soft" && c.dark == DarkMode::Off);
  prefs.set(pref::theme, "gone");
  // And our own keys win, whatever upstream's say.
  prefs.set(pref::skin, "two");
  prefs.set(pref::skin_theme, "new");
  prefs.set(pref::dark_mode, "system");
  c = choice_of(prefs, skins);
  CHECK(c.skin->id == "two" && c.theme->id == "new" && c.dark == DarkMode::System);
  CHECK(prefs.get(pref::theme) == "gone");  // nothing here writes
}

void a_theme_no_longer_there_goes_to_the_default_of_its_skin() {
  const auto skins = packs();
  MemoryPrefs prefs;
  prefs.set(pref::skin, "two");
  prefs.set(pref::skin_theme, "gone");
  CHECK(choice_of(prefs, skins).theme->id == "old");
  prefs.set(pref::skin, "gone");
  CHECK(choice_of(prefs, skins).skin->id == "one");
}

void the_palette_follows_the_mode_and_a_theme_without_dark_stays_light() {
  const auto skins = packs();
  MemoryPrefs prefs;
  prefs.set(pref::skin, "one");
  prefs.set(pref::skin_theme, "sky");
  prefs.set(pref::dark_mode, "off");
  CHECK(choice_of(prefs, skins).palette(true) == "sky");
  prefs.set(pref::dark_mode, "on");
  CHECK(choice_of(prefs, skins).palette(false) == "night");
  prefs.set(pref::dark_mode, "system");
  CHECK(choice_of(prefs, skins).palette(false) == "sky");
  CHECK(choice_of(prefs, skins).palette(true) == "night");
  prefs.set(pref::dark_mode, "on");
  prefs.set(pref::skin_theme, "soft");
  const SkinChoice locked = choice_of(prefs, skins);
  CHECK(!locked.has_dark());
  CHECK(locked.palette(true) == "soft");
  CHECK(locked.dark == DarkMode::On);  // kept for the next Theme
}

// Upstream's name for 뭉게구름, in upstream's keys or where an earlier build saved it.
void cloudroom_is_cumulus() {
  const std::vector<SkinInfo> skins = {{"cloud", "dir/cloud", {}, {theme("cumulus", "darkcloud"), theme("pastel")}, "pastel"}};
  MemoryPrefs prefs;
  prefs.set(pref::theme, "cloudroom");
  CHECK(choice_of(prefs, skins).theme->id == "cumulus");
  prefs.set(pref::skin, "cloud");
  prefs.set(pref::skin_theme, "cloudroom");
  CHECK(choice_of(prefs, skins).theme->id == "cumulus");
}

void a_name_is_by_locale_or_english_or_the_id() {
  const auto skins = packs();
  CHECK(name_in(skins[0].names, "ko") == "하나");
  CHECK(name_in(skins[0].names, "en") == "One");
  CHECK(name_in(skins[0].names, "ja") == "One");
  CHECK(name_in(skins[1].names, "ko") == "Two");
  const std::vector<std::pair<std::string, std::string>> korean_only = {{"ko", "셋"}};
  CHECK(name_in(korean_only, "ja", "three") == "three");
  CHECK(name_in(korean_only, "ja") == "셋");
  CHECK(name_in({}, "ko").empty());
  CHECK(theme_name(skins, "two", "new", "ko") == "new");
  CHECK(theme_name(skins, "two", "none", "ko").empty());
}

}  // namespace

int main() {
  no_packs_choose_nothing();
  upstreams_keys_say_it_until_our_own_are_there();
  a_theme_no_longer_there_goes_to_the_default_of_its_skin();
  the_palette_follows_the_mode_and_a_theme_without_dark_stays_light();
  cloudroom_is_cumulus();
  a_name_is_by_locale_or_english_or_the_id();
  return crpp_test_result();
}
