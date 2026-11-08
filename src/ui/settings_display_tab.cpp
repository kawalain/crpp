#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>

#include "crpp/ui/chat_panel.hpp"
#include "crpp/ui/settings_tabs.hpp"

namespace crpp::ui {

namespace {

constexpr int scale_min = 80, scale_max = 150;  // CHAT_SCALE_MIN, CHAT_SCALE_MAX
constexpr float row_step_theme = 52;
constexpr float sample_w = 420, sample_h = 116;  // real pixels, whatever the stage's scale
constexpr std::array<int, 3> idle_minutes{15, 30, 60};  // IDLE_CHOICES
// Upstream has these on the system tab: from the top of the time section, the language's.
constexpr float language_after_time = 166;

// Saved as a Locale's id, and listed after the entry for following the OS.
int selected_language(const std::string& id) {
  const auto locales = locale::all_locales();
  const auto found = std::ranges::find(locales, std::string_view(id), &locale::Locale::id);
  return found == locales.end() ? 0 : static_cast<int>(found - locales.begin()) + 1;
}

// Which of these Themes a saved id is; the first for an id it does not know.
int index_of(const std::vector<const ThemeEntry*>& list, const std::string& saved) {
  const auto it = std::ranges::find(list, theme_from_upstream(saved), &ThemeEntry::id);
  return it == list.end() ? 0 : static_cast<int>(it - list.begin());
}

std::vector<std::string> labels_of(const std::vector<const ThemeEntry*>& list) {
  std::vector<std::string> labels;
  for (const ThemeEntry* entry : list) labels.emplace_back(tr().*entry->label);
  return labels;
}

}  // namespace

const ThemeEntry& effective_theme(const Prefs& prefs, bool system_dark) {
  if (prefs.get(pref::system_theme)) {
    const auto list = themes_of(system_dark);
    return *list[static_cast<std::size_t>(index_of(list, prefs.get(system_dark ? pref::dark_theme : pref::light_theme)))];
  }
  return theme_named(theme_from_upstream(prefs.get(pref::theme)));
}

DisplayTab::DisplayTab(Ui& ui) : SettingsTab(ui) {
  theme_.id_base = ThemeList;
  light_.id_base = LightList;
  dark_.id_base = DarkList;
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

float DisplayTab::chat_top() const {
  const bool sync = ui_.prefs->get(pref::system_theme);
  return row_top + row_step_theme * (sync ? 2 : 1) + 62 + row_top - 25;
}

// .chat-size-block: the sample, then the label (14 + 26 + 8) and the slider (14). The time
// section's heading stands as far below the slider as a section's first row below its heading.
float DisplayTab::time_top() const { return chat_top() + sample_h / ui_.scale + 14 + 26 + 8 + 14 + 32; }

float DisplayTab::content_height() const {
  if (flows("settings_display")) return flow_height_;
  return time_top() + language_after_time + 50 + dropdown_h + settings_bottom_pad;
}

void DisplayTab::paint(Painter& p, Rect area) {
  if (const skin::Screen* screen = ui_.pack_screen("settings_display")) {
    paint_pack(p, *screen, area);
    return;
  }
  if (!ui_.skin().scales_stage()) {
    paint_luna(p, area);
    return;
  }
  const Theme& t = ui_.theme;
  const Prefs& prefs = *ui_.prefs;
  const bool sync = prefs.get(pref::system_theme);
  const auto every = all_theme_entries(), light = themes_of(false), dark = themes_of(true);
  theme_.options = labels_of(every);
  light_.options = labels_of(light);
  dark_.options = labels_of(dark);
  paint_heading(p, t, area, 25, tr().display_theme);
  paint_toggle(ui_, this, p, area, row_top, tr().display_sync, sync, SystemTheme, system_anim_);

  const auto row = [&](Dropdown& dropdown, std::string_view label, float top, int selected, Rect& rect) {
    dropdown.selected = selected;
    paint_row_label(p, t, area, top + 5, label);
    rect = {area.x + dropdown_x, area.y + top - 1, dropdown_w, dropdown_h};
    dropdown.paint_button(ui_, this, p, rect);
  };
  const float first = row_top + row_step_theme;
  if (sync) {
    row(light_, tr().display_light, first, index_of(light, prefs.get(pref::light_theme)), list_rect_[1]);
    row(dark_, tr().display_dark, first + row_step_theme, index_of(dark, prefs.get(pref::dark_theme)), list_rect_[2]);
  } else {
    row(theme_, tr().display_general, first, index_of(every, prefs.get(pref::theme)), list_rect_[0]);
  }

  const float top = chat_top();
  paint_heading(p, t, area, top - (row_top - 25), tr().room_word_chat);

  // The sample cancels the stage's scale: it shows what the chat, which is not
  // scaled with the window, will really look like.
  const int percent = std::clamp(prefs.get(pref::chat_scale), scale_min, scale_max);
  const float k = ui_.scale;
  const Rect box{area.x + 8, area.y + top, sample_w / k, sample_h / k};
  p.fill_round(box, 14, with_alpha(t.hl, 0.1f));
  paint_sample(p, box, static_cast<float>(percent) / 100);

  const float label_y = top + box.h + 14;
  paint_row_label(p, t, area, label_y, tr().display_chat_size);
  slider_ = {area.x + 16, area.y + label_y + 26 + 8, 244, 14};
  paint_slider(p, t, slider_, percent, scale_min, scale_max);
  ui_.add_hit(this, {slider_.x, slider_.y - 4, slider_.w, slider_.h + 8}, ScaleSlider);

  // .volume-tooltip, centered over the thumb.
  slider_tip_.set(ui_, ui_.hot(this, ScaleSlider) || ui_.capture == this, 120);
  if (const float shown = slider_tip_.value(ui_); shown > 0) {
    char text[8];
    std::snprintf(text, sizeof text, "%d%%", percent);
    const TextStyle style{.size = 15, .line_height = 20};
    const float w = text_width(p, text, style) + 18;
    const float at = static_cast<float>(percent - scale_min) / static_cast<float>(scale_max - scale_min);
    const float cx = slider_.x + 10.5f + at * (slider_.w - 21);
    paint_bubble(p, t, text, {cx - w / 2, slider_.y - 26 - 11, w, 26}, style, 13, rgb(0x75ccff), rgb(0xFFFFFF), shown);
  }

  // Upstream's time and language sections, as they are on its system tab.
  const float time = time_top();
  paint_heading(p, t, area, time, tr().system_time);
  paint_toggle(ui_, this, p, area, time + 51, tr().system_24h, prefs.get(pref::use_24h), Use24h, use_24h_anim_);
  const float idle_top = time + 51 + row_step;
  paint_row_label(p, t, area, idle_top + 5, tr().system_idle);
  sync_idle();
  idle_rect_ = {area.x + dropdown_x, area.y + idle_top - 1, dropdown_w, dropdown_h};
  idle_.paint_button(ui_, this, p, idle_rect_);

  const float language_top = time + language_after_time;
  paint_heading(p, t, area, language_top, tr().system_language);
  paint_row_label(p, t, area, language_top + 51 + 5, tr().system_language_row);
  sync_language();
  language_rect_ = {area.x + dropdown_x, area.y + language_top + 51 - 1, dropdown_w, dropdown_h};
  language_.paint_button(ui_, this, p, language_rect_);
}

void DisplayTab::paint_sample(Painter& p, Rect box, float zoom) {
  const Theme& t = ui_.theme;
  const D2D1_MATRIX_3X2_F stage = D2D1::Matrix3x2F::Scale(ui_.scale, ui_.scale) * D2D1::Matrix3x2F::Translation(ui_.ox, ui_.oy);
  p.rt()->PushAxisAlignedClip(D2D1::RectF(box.x, box.y, box.right(), box.bottom()), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  const float s = zoom / ui_.scale;
  p.set_base(D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(box.x, box.y) * stage);

  // .chat-sample .chat-list has padding 14 18; the rest is .chat-head and .chat-text.
  const float x = 18, y = 14, inner_w = sample_w / zoom - 36;
  draw_svg(p, ui_.emoji_image(sample_emoji_), {x, y, 25, 25});
  const std::string time = chat_clock_text(wall_clock_seconds() * 1000, ui_.prefs->get(pref::use_24h));
  const D2D1_SIZE_F time_size = p.text(time, {x, y + 2.5f, inner_w, 20}, {.size = 14, .line_height = 20, .align = Align::Right}, t.hl_text);
  p.text(sample_nick_.empty() ? std::string_view(tr().guest) : std::string_view(sample_nick_),
         {x + 25 + 7, y + 2.5f, std::max(20.0f, inner_w - 25 - 7 - time_size.width - 7), 20},
         {.size = 15, .line_height = 20, .ellipsis = true}, t.hl_text);
  p.text(tr().display_chat_sample, {x, y + 25 + 2, 2000, 19}, {.size = 17, .line_height = 19}, t.hl_text);

  p.set_base(stage);
  p.rt()->PopAxisAlignedClip();
}

void DisplayTab::paint_popups(Painter& p, Rect) {
  idle_.paint_list(ui_, this, p, idle_rect_);
  language_.paint_list(ui_, this, p, language_rect_);
  if (ui_.pack_screen("settings_display")) {
    skin_theme_.paint_list(ui_, this, p, list_rect_[4]);
    dark_mode_.paint_list(ui_, this, p, list_rect_[5]);
    return;
  }
  if (ui_.prefs->get(pref::system_theme)) {
    light_.paint_list(ui_, this, p, list_rect_[1]);
    dark_.paint_list(ui_, this, p, list_rect_[2]);
  } else {
    theme_.paint_list(ui_, this, p, list_rect_[0]);
  }
}

void DisplayTab::before_click(const Surface* owner, int id) {
  for (Dropdown* d : {&theme_, &light_, &dark_, &skin_theme_, &dark_mode_, &idle_, &language_}) {
    if (owner != this || !d->owns(id)) d->open = false;
  }
}

bool DisplayTab::acts_on_press(int id) const {
  if (id == ScaleSlider) return true;
  for (const Dropdown* d : {&theme_, &light_, &dark_, &skin_theme_, &dark_mode_, &idle_, &language_}) {
    if (id == d->id_base) return true;
  }
  return false;
}

void DisplayTab::click(int id, float x, float, bool) {
  Prefs& prefs = *ui_.prefs;
  if (id == SystemTheme) {
    prefs.set(pref::system_theme, !prefs.get(pref::system_theme));
  } else if (id == ThemeCursor) {
    prefs.set(pref::theme_cursor, !prefs.get(pref::theme_cursor));
  } else if (id == Use24h) {
    prefs.set(pref::use_24h, !prefs.get(pref::use_24h));
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
  } else if (theme_.owns(id)) {
    if (theme_.click(id)) prefs.set(pref::theme, theme_to_upstream(all_theme_entries()[static_cast<std::size_t>(theme_.selected)]->id));
    if (theme_.open) place_dropdown(ui_, theme_, list_rect_[0]);
  } else if (light_.owns(id)) {
    if (light_.click(id)) prefs.set(pref::light_theme, theme_to_upstream(themes_of(false)[static_cast<std::size_t>(light_.selected)]->id));
    if (light_.open) place_dropdown(ui_, light_, list_rect_[1]);
  } else if (dark_.owns(id)) {
    if (dark_.click(id)) prefs.set(pref::dark_theme, theme_to_upstream(themes_of(true)[static_cast<std::size_t>(dark_.selected)]->id));
    if (dark_.open) place_dropdown(ui_, dark_, list_rect_[2]);
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
  const SkinInfo& skin = ui_.skins[static_cast<std::size_t>(skin_index)];
  Prefs& prefs = *ui_.prefs;
  prefs.set(pref::dark_mode, dark_key(choice_of(prefs, ui_.skins).dark));
  prefs.set(pref::skin, skin.id);
  prefs.set(pref::skin_theme, skin.themes[static_cast<std::size_t>(theme_index)].id);
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
  return theme_.wheel(ui_, x, y, delta) || light_.wheel(ui_, x, y, delta) || dark_.wheel(ui_, x, y, delta) ||
         skin_theme_.wheel(ui_, x, y, delta) || dark_mode_.wheel(ui_, x, y, delta) || idle_.wheel(ui_, x, y, delta) || language_.wheel(ui_, x, y, delta);
}

bool DisplayTab::escape() {
  bool closed = false;
  for (Dropdown* d : {&theme_, &light_, &dark_, &skin_theme_, &dark_mode_, &idle_, &language_}) {
    closed = closed || d->open;
    d->open = false;
  }
  return closed;
}

bool DisplayTab::animating() const { return system_anim_.running(ui_) || use_24h_anim_.running(ui_) || slider_tip_.running(ui_); }

}  // namespace crpp::ui
