#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>

#include "crpp/ui/chat_panel.hpp"
#include "crpp/ui/settings_tabs.hpp"

namespace crpp::ui {

namespace {

constexpr int scale_min = 80, scale_max = 150;  // CHAT_SCALE_MIN, CHAT_SCALE_MAX
constexpr std::array<int, 3> idle_minutes{15, 30, 60};  // IDLE_CHOICES

// Saved as a Locale's id, and listed after the entry for following the OS.
int selected_language(const std::string& id) {
  const auto locales = locale::all_locales();
  const auto found = std::ranges::find(locales, std::string_view(id), &locale::Locale::id);
  return found == locales.end() ? 0 : static_cast<int>(found - locales.begin()) + 1;
}

}  // namespace

DisplayTab::DisplayTab(Ui& ui) : SettingsTab(ui) {
  skin_theme_.id_base = SkinThemeList;
  dark_mode_.id_base = DarkModeList;
  idle_.id_base = IdleList;
  language_.id_base = LanguageList;
}

void DisplayTab::sync_idle() {
  idle_.options = {tr().system_idle_15, tr().system_idle_30, tr().system_idle_60};
  const int minutes = ui_.prefs->get(pref::idle_minutes);
  idle_.selected = 1;  // upstream takes anything else as 30 minutes
  for (std::size_t i = 0; i < idle_minutes.size(); ++i) {
    if (idle_minutes[i] == minutes) idle_.selected = static_cast<int>(i);
  }
}

void DisplayTab::sync_language() {
  language_.options = {tr().system_language_auto};
  for (const locale::Locale& l : locale::all_locales()) language_.options.emplace_back(l.name);
  language_.selected = selected_language(ui_.prefs->get(pref::language));
}

void DisplayTab::set_sample(std::string nick, std::string emoji) {
  sample_nick_ = std::move(nick);
  sample_emoji_ = std::move(emoji);
}

void DisplayTab::shown() {
  if (on_shown) on_shown();
}

void DisplayTab::paint(Painter& p, Rect area) {
  if (const skin::Screen* screen = ui_.pack_screen("settings_display")) paint_pack(p, *screen, area);
}

void DisplayTab::paint_popups(Painter& p, Rect) {
  idle_.paint_list(ui_, this, p, idle_rect_);
  language_.paint_list(ui_, this, p, language_rect_);
  skin_theme_.paint_list(ui_, this, p, list_rect_[4]);
  dark_mode_.paint_list(ui_, this, p, list_rect_[5]);
}

void DisplayTab::before_click(const Surface* owner, int id) {
  for (Dropdown* d : {&skin_theme_, &dark_mode_, &idle_, &language_}) {
    if (owner != this || !d->owns(id)) d->open = false;
  }
}

bool DisplayTab::acts_on_press(int id) const {
  if (id == ScaleSlider) return true;
  for (const Dropdown* d : {&skin_theme_, &dark_mode_, &idle_, &language_}) {
    if (id == d->id_base) return true;
  }
  return false;
}

void DisplayTab::click(int id, float x, float, bool) {
  Prefs& prefs = *ui_.prefs;
  if (id == ThemeCursor) {
    prefs.set(pref::theme_cursor, !prefs.get(pref::theme_cursor));
  } else if (id == Use24h) {
    prefs.set(pref::use_24h, !prefs.get(pref::use_24h));
  } else if (id == FetchCancel) {
    if (on_cancel_fetch) on_cancel_fetch();
  } else if (id == FetchRetry) {
    const ThemeFetch fetch = ui_.theme_fetch;
    if (on_fetch && fetch.state == ThemeFetch::State::Failed) on_fetch(fetch.skin, fetch.theme);
  } else if (idle_.owns(id)) {
    if (idle_.click(id)) prefs.set(pref::idle_minutes, idle_minutes[static_cast<std::size_t>(idle_.selected)]);
    if (idle_.open) place_dropdown(ui_, idle_, idle_rect_);
  } else if (language_.owns(id)) {
    if (language_.click(id)) {
      const auto locales = locale::all_locales();
      prefs.set(pref::language, language_.selected == 0 ? std::string_view{} : locales[static_cast<std::size_t>(language_.selected - 1)].id);
    }
    if (language_.open) place_dropdown(ui_, language_, language_rect_);
  } else if (id == ScaleSlider) {
    ui_.capture = this;
    drag(x, 0);
  } else if (skin_theme_.owns(id)) {
    if (skin_theme_.click(id)) choose_theme(skin_theme_.selected);
    if (skin_theme_.open) place_dropdown(ui_, skin_theme_, list_rect_[4]);
  } else if (dark_mode_.owns(id)) {
    if (dark_mode_.click(id)) choose_dark(dark_mode_.selected);
    if (dark_mode_.open) place_dropdown(ui_, dark_mode_, list_rect_[5]);
  }
}

namespace {

constexpr std::string_view dark_keys[] = {"off", "on", "system"};

std::string_view dark_key(DarkMode mode) { return dark_keys[static_cast<std::size_t>(mode)]; }

}  // namespace

// All three go in together, so that this client's own keys say everything from here on. The
// Theme comes last, as that is what makes the program apply them. The dark mode stays.
void DisplayTab::choose_theme(int index) {
  const auto [skin_index, theme_index] = theme_at_[static_cast<std::size_t>(index)];
  if (skin_index < 0) return;  // a heading, which cannot be chosen
  ThemeFetch& fetch = ui_.theme_fetch;
  const std::size_t installed = ui_.skins.size();
  if (static_cast<std::size_t>(skin_index) >= installed) {
    const SkinInfo& skin = ui_.offered[static_cast<std::size_t>(skin_index) - installed];
    if (on_fetch && skin.needs.empty()) on_fetch(skin.id, skin.themes[static_cast<std::size_t>(theme_index)].id);
    return;
  }
  // Another Theme now: a pack still arriving stays installed without being applied.
  fetch.apply = false;
  if (fetch.state == ThemeFetch::State::Failed) fetch = {};
  const SkinInfo& skin = ui_.skins[static_cast<std::size_t>(skin_index)];
  use_theme(skin.id, skin.themes[static_cast<std::size_t>(theme_index)].id);
}

void DisplayTab::use_theme(const std::string& skin, const std::string& theme) {
  Prefs& prefs = *ui_.prefs;
  prefs.set(pref::dark_mode, dark_key(choice_of(prefs, ui_.skins).dark));
  prefs.set(pref::skin, skin);
  prefs.set(pref::skin_theme, theme);
}

void DisplayTab::choose_dark(int index) {
  Prefs& prefs = *ui_.prefs;
  const SkinChoice choice = choice_of(prefs, ui_.skins);
  prefs.set(pref::skin, choice.skin->id);
  prefs.set(pref::skin_theme, choice.theme->id);
  prefs.set(pref::dark_mode, dark_keys[static_cast<std::size_t>(index)]);
}

void DisplayTab::drag(float x, float) {
  const int value = slider_value(slider_, ui_.to_stage_x(x), scale_min, scale_max);
  if (value != ui_.prefs->get(pref::chat_scale)) ui_.prefs->set(pref::chat_scale, value);
}

bool DisplayTab::wheel(float x, float y, float delta) {
  return skin_theme_.wheel(ui_, x, y, delta) || dark_mode_.wheel(ui_, x, y, delta) || idle_.wheel(ui_, x, y, delta) || language_.wheel(ui_, x, y, delta);
}

bool DisplayTab::stop_key(int id, unsigned vk, bool ctrl, bool, bool alt) {
  if (ctrl) return false;
  if (id == ScaleSlider) {
    const std::optional<int> value = slider_key(vk, ui_.prefs->get(pref::chat_scale), scale_min, scale_max);
    if (!value) return false;
    if (*value != ui_.prefs->get(pref::chat_scale)) ui_.prefs->set(pref::chat_scale, *value);
    return true;
  }
  for (Dropdown* box : {&skin_theme_, &dark_mode_, &idle_, &language_}) {
    if (box->owns(id)) {
      const std::optional<int> hit = box->key(ui_, this, vk, alt);
      if (!hit) return false;
      if (*hit != Dropdown::taken_alone) click(*hit, 0, 0, false);
      return true;
    }
  }
  return false;
}

bool DisplayTab::escape() {
  bool closed = false;
  for (Dropdown* d : {&skin_theme_, &dark_mode_, &idle_, &language_}) {
    closed = closed || d->open;
    d->open = false;
  }
  return closed;
}


}  // namespace crpp::ui
