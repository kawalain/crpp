#include <algorithm>
#include <cstdio>
#include <ctime>

#include "crpp/ui/chat_panel.hpp"
#include "crpp/ui/settings_tabs.hpp"

namespace crpp::ui {

namespace {

constexpr int scale_min = 80, scale_max = 150;  // CHAT_SCALE_MIN, CHAT_SCALE_MAX
constexpr float row_step_theme = 52;
constexpr float sample_w = 420, sample_h = 116;  // real pixels, whatever the stage's scale

// Which of these Themes a saved id is; the first for an id it does not know.
int index_of(const std::vector<const ThemeEntry*>& list, const std::string& id) {
  const auto it = std::ranges::find(list, std::string_view(id), &ThemeEntry::id);
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
  return theme_named(prefs.get(pref::theme));
}

DisplayTab::DisplayTab(Ui& ui) : SettingsTab(ui) {
  theme_.id_base = ThemeList;
  light_.id_base = LightList;
  dark_.id_base = DarkList;
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

float DisplayTab::content_height() const {
  if (!ui_.skin().scales_stage()) return flow_height_;
  // .chat-size-block: the sample, then the label (14 + 26 + 8) and the slider (14).
  return chat_top() + sample_h / ui_.scale + 14 + 26 + 8 + 14 + settings_bottom_pad;
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
  if (ui_.prefs->get(pref::system_theme)) {
    light_.paint_list(ui_, this, p, list_rect_[1]);
    dark_.paint_list(ui_, this, p, list_rect_[2]);
  } else {
    theme_.paint_list(ui_, this, p, list_rect_[0]);
  }
}

void DisplayTab::before_click(const Surface* owner, int id) {
  for (Dropdown* d : {&theme_, &light_, &dark_}) {
    if (owner != this || !d->owns(id)) d->open = false;
  }
}

void DisplayTab::click(int id, float x, float, bool) {
  Prefs& prefs = *ui_.prefs;
  if (id == SystemTheme) {
    prefs.set(pref::system_theme, !prefs.get(pref::system_theme));
  } else if (id == ThemeCursor) {
    prefs.set(pref::theme_cursor, !prefs.get(pref::theme_cursor));
  } else if (id == ScaleSlider) {
    ui_.capture = this;
    drag(x, 0);
  } else if (theme_.owns(id)) {
    if (theme_.click(id)) prefs.set(pref::theme, all_theme_entries()[static_cast<std::size_t>(theme_.selected)]->id);
    if (theme_.open) place_dropdown(ui_, theme_, list_rect_[0]);
  } else if (light_.owns(id)) {
    if (light_.click(id)) prefs.set(pref::light_theme, themes_of(false)[static_cast<std::size_t>(light_.selected)]->id);
    if (light_.open) place_dropdown(ui_, light_, list_rect_[1]);
  } else if (dark_.owns(id)) {
    if (dark_.click(id)) prefs.set(pref::dark_theme, themes_of(true)[static_cast<std::size_t>(dark_.selected)]->id);
    if (dark_.open) place_dropdown(ui_, dark_, list_rect_[2]);
  }
}

void DisplayTab::drag(float x, float) {
  const int value = slider_value(slider_, ui_.to_stage_x(x), scale_min, scale_max);
  if (value != ui_.prefs->get(pref::chat_scale)) ui_.prefs->set(pref::chat_scale, value);
}

bool DisplayTab::wheel(float x, float y, float delta) {
  return theme_.wheel(ui_, x, y, delta) || light_.wheel(ui_, x, y, delta) || dark_.wheel(ui_, x, y, delta);
}

bool DisplayTab::escape() {
  bool closed = false;
  for (Dropdown* d : {&theme_, &light_, &dark_}) {
    closed = closed || d->open;
    d->open = false;
  }
  return closed;
}

bool DisplayTab::animating() const { return system_anim_.running(ui_) || slider_tip_.running(ui_); }

}  // namespace crpp::ui
