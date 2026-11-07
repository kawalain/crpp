#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "crpp/ui/prefs.hpp"
#include "crpp/ui/settings_tabs.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

constexpr skin::Name slider_tip_text_names[] = {{"text", 0}};
constexpr skin::Name slider_tip_number_names[] = {{"track_x", 0}, {"track_y", 1}, {"track_w", 2}, {"percent", 3}, {"on", 4}};

// Vertical positions are main.js settingsContent's. Without the Pen row everything
// below it moves up by this much.
constexpr float pen_shift = show_pen ? 0 : 69;
constexpr float effects_shift = 104 - pen_shift;

// .settings-content: where a dropdown has room for its list.
constexpr float view_top = 137, view_bottom = 641;

constexpr float slider_x = 16, slider_w = 244, slider_hit_h = 21;
constexpr float row_x = 16, row_w = 505, row_h = 36, switch_w = 50, switch_h = 26;

constexpr int balance_min = 55, balance_max = 90;  // BALANCE_MIN, BALANCE_MAX
constexpr float balance_top = 749 + effects_shift;  // upstream 725, and a line of explanation
// .balance-control: 385 wide, centered in the 550 column, 60 high, with a person on
// each side of the slider and gaps of 10.
constexpr float reverb_top = balance_top + 60 + 16;
constexpr float person_w = 46, person_h = 62, balance_left = (550 - 385) / 2.0f;
constexpr float balance_slider_x = balance_left + person_w + 10, balance_slider_w = 385 - 2 * person_w - 20;
constexpr float balance_slider_h = 39, balance_thumb_w = 29.15f;

constexpr float reverb_dropdown_top = reverb_top + (row_h - dropdown_h) / 2;

constexpr float knob_ms = 160, tip_ms = 120;
constexpr float off_alpha = 0.42f;  // .balance-control.disabled

// .person b: 46 x 37, radii 23 on top and 4 below.
constexpr const char* person_body_path = "M0 23A23 23 0 0 1 46 23V33A4 4 0 0 1 42 37H4A4 4 0 0 1 0 33Z";
// .balance-control input's thumb mask, viewBox -9.8 0 349.8 468.
constexpr const char* balance_thumb_path =
    "M305.1,287.000 C295.016,299.989 282.766,313.682 265.1,315.000 C251.730,316.122 255.120,296.759 263.000,289.000 "
    "C272.380,279.765 279.802,242.670 271.000,217.000 C262.198,191.330 230.460,157.326 192.1,149.1 L192.1,335.793 "
    "C193.345,336.601 193.706,337.398 194.028,338.222 C212.363,385.188 185.104,439.707 133.143,459.993 "
    "C81.182,480.278 24.195,458.649 5.859,411.683 C-12.477,364.717 14.782,310.198 66.744,289.913 "
    "C91.239,280.350 116.847,280.109 138.1,287.439 L138.1,8.1 C138.1,4.029 143.029,-0.000 148.000,-0.000 "
    "L192.1,1.1 C192.1,1.1 261.344,50.059 289.1,76.000 C325.180,107.847 333.327,134.014 339.000,166.000 "
    "C344.600,197.570 325.823,263.557 305.1,287.000 Z";
constexpr float thumb_view_x = 9.8f, thumb_view_w = 349.8f;

struct VolumeRow {
  const char* locale::Strings::*label;
  pref::Int key;
  float label_top, slider_top, dropdown_top;
};
constexpr std::array<VolumeRow, 4> volume_rows{{
    {&locale::Strings::sound_speaker_volume, pref::volume, 81 - 13, 109 - 7, 94 - 19},
    {&locale::Strings::sound_keyboard_volume, pref::keyboard_volume, 227 - 13, 256 - 7, 239.5f - 19},
    {&locale::Strings::sound_mouse_volume, pref::mouse_volume, 296 - 13, 325 - 7, 309 - 19},
    {&locale::Strings::sound_pen_volume, pref::pen_volume, 365 - 13, 394 - 7, 379 - 19},
}};

struct ToggleRow {
  SoundTab::Id id;
  const char* locale::Strings::*label;
  pref::Bool key;
  float top;
};
constexpr std::array<ToggleRow, 6> toggle_rows{{
    {SoundTab::ListenSelf, &locale::Strings::sound_listen_self, pref::listen_self, 424 - pen_shift},
    {SoundTab::OthersUseMine, &locale::Strings::sound_others_use_mine, pref::others_use_my_sounds, 476 - pen_shift},
    {SoundTab::MuteNotifications, &locale::Strings::sound_mute_notifications, pref::mute_notifications_on_mute, 528 - pen_shift},
    {SoundTab::MuteOnIdle, &locale::Strings::sound_mute_on_idle, pref::mute_on_idle, 580 - pen_shift},
    {SoundTab::Spatial, &locale::Strings::sound_spatial, pref::spatial_audio, 581 + effects_shift},
    {SoundTab::BalanceEnabled, &locale::Strings::sound_balance_enabled, pref::channel_balance_enabled, 633 + effects_shift},
}};

constexpr bool row_shown(std::size_t row) { return show_pen || row < 3; }

}  // namespace

std::string_view balance_word(int value) {
  if (value <= 65) return tr().sound_balance_weak;
  return value <= 78 ? tr().sound_balance_medium : tr().sound_balance_strong;
}

SoundTab::SoundTab(Ui& ui) : SettingsTab(ui) {
  speaker_.box.id_base = SpeakerList;
  packs_[0].box.id_base = KeyboardList;
  packs_[1].box.id_base = MouseList;
  packs_[2].box.id_base = PenList;
  reverb_.box.id_base = ReverbList;
  reverb_.ids = {"off", "office", "large"};
  refresh_speakers();
}

void SoundTab::shown() {
  close_lists(nullptr);
  if (on_shown) on_shown();
}

void SoundTab::set_speakers(std::vector<SoundChoice> speakers) {
  speakers_ = std::move(speakers);
  refresh_speakers();
}

void SoundTab::set_packs(SoundDevice device, std::vector<SoundChoice> packs) {
  Picker& picker = packs_[static_cast<std::size_t>(device)];
  picker.ids.clear();
  picker.box.options.clear();
  for (SoundChoice& pack : packs) {
    picker.ids.push_back(std::move(pack.id));
    picker.box.options.push_back(std::move(pack.name));
  }
}

void SoundTab::refresh_speakers() {
  const std::string current = ui_.prefs ? ui_.prefs->get(pref::speaker_device) : std::string{};
  speaker_.ids = {""};
  speaker_.box.options = {tr().sound_default_output};
  // A saved device that is away stays on the list, so the choice can be seen.
  if (!current.empty() && std::ranges::none_of(speakers_, [&](const SoundChoice& c) { return c.id == current; })) {
    const std::string name = ui_.prefs->get(pref::speaker_device_name);
    speaker_.ids.push_back(current);
    speaker_.box.options.push_back(name.empty() ? std::string(tr().sound_current_output) : name);
  }
  for (const SoundChoice& speaker : speakers_) {
    speaker_.ids.push_back(speaker.id);
    speaker_.box.options.push_back(speaker.name);
  }
}

void SoundTab::sync_selection() {
  const auto index_of = [](const Picker& picker, const std::string& id) {
    const auto found = std::ranges::find(picker.ids, id);
    return found == picker.ids.end() ? -1 : static_cast<int>(found - picker.ids.begin());
  };
  const std::string speaker = ui_.prefs->get(pref::speaker_device);
  if (index_of(speaker_, speaker) < 0) refresh_speakers();
  speaker_.box.selected = std::max(0, index_of(speaker_, speaker));
  packs_[0].box.selected = std::max(0, index_of(packs_[0], ui_.prefs->get(pref::keyboard_sound)));
  packs_[1].box.selected = std::max(0, index_of(packs_[1], ui_.prefs->get(pref::mouse_sound)));
  packs_[2].box.selected = std::max(0, index_of(packs_[2], ui_.prefs->get(pref::pen_sound)));
  // A name this version does not know sounds like the default, so it shows as that.
  const int reverb = index_of(reverb_, ui_.prefs->get(pref::reverb));
  reverb_.box.selected = reverb >= 0 ? reverb : index_of(reverb_, pref::reverb.fallback);
}

SoundTab::Picker* SoundTab::picker_of_row(std::size_t row) {
  return row == 0 ? &speaker_ : &packs_[row - 1];
}

void SoundTab::paint_tip(Painter& p, Rect area) {
  // Follows the hover or the drag; fades both ways (.volume-tooltip).
  int hovered = -1;
  for (std::size_t row = 0; row < volume_rows.size(); ++row) {
    const int id = SpeakerVolume + static_cast<int>(row);
    if (row_shown(row) && (held_ == id || ui_.hot(this, id))) hovered = static_cast<int>(row);
  }
  const bool on = hovered >= 0;
  if (on) tip_row_ = hovered;
  if (on != tip_on_) {
    tip_on_ = on;
    tip_start_ = ui_.now_ms;
  }
  if (const skin::Screen* screen = ui_.pack_screen("slider_tip")) {
    if (tip_row_ < 0) return;
    const auto row = static_cast<std::size_t>(tip_row_);
    const int value = ui_.prefs->get(volume_rows[row].key);
    const std::string text = std::to_string(value) + "%";
    class Shown final : public skin::Source {
     public:
      Shown(std::string_view words, Rect track, int percent, bool on) : text_(words), track_(track), percent_(percent), on_(on) {}
      std::string_view text(int, int) override { return text_; }
      float number(int id, int) override {
        switch (id) {
          case 0: return track_.x;
          case 1: return track_.y;
          case 2: return track_.w;
          case 4: return on_ ? 1.0f : 0.0f;
          default: return static_cast<float>(percent_);
        }
      }

     private:
      std::string_view text_;
      Rect track_;
      int percent_;
      bool on_;
    } shown(text, slider_rects_[row], value, on);
    draw_screen(ui_, p, this, *screen, {0, 0, ui_.width, ui_.height}, shown);
    return;
  }
  const float progress = transition(ui_, tip_start_, tip_ms);
  const float opacity = tip_on_ ? progress : 1 - progress;
  if (opacity <= 0 || tip_row_ < 0) return;
  if (!ui_.skin().scales_stage()) {
    paint_tip_luna(p, tip_row_);
    return;
  }

  const VolumeRow& row = volume_rows[static_cast<std::size_t>(tip_row_)];
  const int value = ui_.prefs->get(row.key);
  char text[8];
  std::snprintf(text, sizeof text, "%d%%", value);
  const std::wstring label = widen(text);
  float text_w = 40;
  if (const auto layout = ui_.gfx.layout(label, {.size = 15, .line_height = 20}, 200)) {
    DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(layout->GetMetrics(&metrics))) text_w = metrics.widthIncludingTrailingWhitespace;
  }
  constexpr float thumb = 21;
  const float w = text_w + 18, h = 26;
  const float cx = area.x + slider_x + thumb / 2 + static_cast<float>(value) / 100 * (slider_w - thumb);
  const Rect r{cx - w / 2, area.y + row.slider_top - h - 11, w, h};
  p.shadow_round(r, 13, 8, 2, rgb(0x2e648c, 0.2f * opacity));
  p.fill_round(r, 13, rgb(0x75ccff, opacity));
  p.text(label, r, {.size = 15, .line_height = h, .align = Align::Center}, rgb(0xffffff, opacity));
}

void SoundTab::paint_person(Painter& p, float x, float y, Color c, bool marked) {
  p.fill_ellipse(x + 23, y + 14, 14, 14, c);  // .person i
  p.push(D2D1::Matrix3x2F::Translation(x, y + person_h - 37));
  p.fill_geometry(ui_.gfx.path(person_body_path), c);
  p.pop();
  if (marked) {
    p.text(tr().sound_me, {x, y + person_h - 37 + 11, person_w, 20}, {.size = 16, .weight = 700, .line_height = 20, .align = Align::Center},
           with_alpha(ui_.theme.on_accent, c.a));
  }
}

void SoundTab::paint_balance(Painter& p, Rect area) {
  const Theme& t = ui_.theme;
  // Without the surround every Desk is centred, so this would change nothing.
  const bool on = ui_.prefs->get(pref::spatial_audio) && ui_.prefs->get(pref::channel_balance_enabled);
  const float a = on ? 1 : off_alpha;
  const int value = std::clamp(ui_.prefs->get(pref::channel_balance), balance_min, balance_max);

  const Rect title{area.x + 16, area.y + 687 + effects_shift, row_w, 24};
  p.text(tr().sound_balance, title, {.size = 16, .line_height = 24}, with_alpha(t.hl_text, a));
  p.text(on ? balance_word(value) : std::string_view(tr().sound_balance_off), title,
         {.size = 16, .weight = 700, .line_height = 24, .align = Align::Right}, with_alpha(t.hl_text, a));
  p.text(tr().sound_balance_hint, {title.x, title.y + 24, row_w, 20}, {.size = 14, .line_height = 20},
         with_alpha(t.hl_text, a * 0.75f));

  const float top = area.y + balance_top;
  const Color ink = with_alpha(t.hl, a);
  paint_person(p, area.x + balance_left, top - 1, ink, true);
  paint_person(p, area.x + balance_slider_x + balance_slider_w + 10, top - 1, ink, false);

  const Rect slider{area.x + balance_slider_x, top + (60 - balance_slider_h) / 2, balance_slider_w, balance_slider_h};
  const float ratio_at = static_cast<float>(value - balance_min) / static_cast<float>(balance_max - balance_min);
  const Rect rail{slider.x + balance_thumb_w / 2, slider.y + slider.h / 2 - 3, slider.w - balance_thumb_w, 6};
  p.fill_round(rail, 3, with_alpha(t.hl_pale2, a));
  if (ratio_at > 0) p.fill_round({rail.x, rail.y, rail.w * ratio_at, rail.h}, 3, ink);
  const float thumb_x = slider.x + ratio_at * (slider.w - balance_thumb_w);
  const float scale = balance_thumb_w / thumb_view_w;
  p.push(D2D1::Matrix3x2F::Translation(thumb_view_x, 0) * D2D1::Matrix3x2F::Scale(scale, scale) *
         D2D1::Matrix3x2F::Translation(thumb_x, slider.y));
  p.fill_geometry(ui_.gfx.path(balance_thumb_path), ink);
  p.pop();
  if (on) ui_.add_hit(this, slider, Balance);
}

void SoundTab::paint(Painter& p, Rect area) {
  if (const skin::Screen* screen = ui_.pack_screen("settings_sound")) {
    paint_pack(p, *screen, area);
    return;
  }
  if (!ui_.skin().scales_stage()) {
    paint_luna(p, area);
    return;
  }
  reverb_.box.options = {tr().sound_reverb_off, tr().sound_reverb_office, tr().sound_reverb_large};
  speaker_.box.options[0] = tr().sound_default_output;
  const Theme& t = ui_.theme;
  last_area_ = area;
  sync_selection();

  const auto heading = [&](const char* text, float top) {
    p.text(text, {area.x + 9, area.y + top, 500, 32}, {.size = 26, .weight = 700, .line_height = 32}, t.hl_text);
  };
  heading(tr().sound_speaker, 25);
  heading(tr().sound_devices, 167);
  heading(tr().sound_effects, 535 + effects_shift);

  for (std::size_t i = 0; i < volume_rows.size(); ++i) {
    if (!row_shown(i)) continue;
    const VolumeRow& row = volume_rows[i];
    p.text(tr().*row.label, {area.x + slider_x, area.y + row.label_top, 300, 26}, {.size = 18, .line_height = 26}, t.hl_text);
    const Rect slider{area.x + slider_x, area.y + row.slider_top + 7 - slider_hit_h / 2, slider_w, slider_hit_h};
    paint_slider(p, t, slider, ui_.prefs->get(row.key), 0, 100);
    ui_.add_hit(this, slider, SpeakerVolume + static_cast<int>(i));
    const Rect button{area.x + dropdown_x, area.y + row.dropdown_top, dropdown_w, dropdown_h};
    picker_of_row(i)->box.paint_button(ui_, this, p, button);
  }

  for (const ToggleRow& row : toggle_rows) {
    const float y = area.y + row.top;
    const bool locked = row.id == BalanceEnabled && !ui_.prefs->get(pref::spatial_audio);
    p.text(tr().*row.label, {area.x + row_x, y, row_w - switch_w - 10, row_h}, {.size = 18, .line_height = row_h},
           with_alpha(t.hl_text, locked ? off_alpha : 1));
    const Rect r{area.x + row_x + row_w - switch_w, y + (row_h - switch_h) / 2, switch_w, switch_h};
    const bool on = ui_.prefs->get(row.key);
    float at = on ? 1.0f : 0.0f;
    if (knob_id_ == row.id) {
      const float moved = transition(ui_, knob_start_, knob_ms);
      at = on ? moved : 1 - moved;
    }
    paint_switch_at(p, t, r, on, at, locked, ui_.hot(this, row.id));
    if (!locked) ui_.add_hit(this, r, row.id);
  }

  paint_balance(p, area);
  p.text(tr().sound_reverb, {area.x + row_x, area.y + reverb_top, row_w, row_h}, {.size = 18, .line_height = row_h}, t.hl_text);
  reverb_.box.paint_button(ui_, this, p, {area.x + dropdown_x, area.y + reverb_dropdown_top, dropdown_w, dropdown_h});
  paint_tip(p, area);
}

skin::ScreenSchema SoundTab::tip_schema() {
  return {.screen = "slider_tip", .texts = slider_tip_text_names, .numbers = slider_tip_number_names};
}

void SoundTab::paint_tip_luna(Painter& p, int row_index) {
  const auto row = static_cast<std::size_t>(row_index);
  const int value = ui_.prefs->get(volume_rows[row].key);
  const std::string text = std::to_string(value) + "%";
  const float w = text_width(p, text, {.size = ui_.gfx.classic_size(), .exact = true}) + 10;
  const Rect s = slider_rects_[row];
  const float cx = s.x + slider_thumb / 2 + static_cast<float>(value) / 100 * (s.w - slider_thumb);
  paint_bubble(p, ui_.theme, text, {std::floor(cx - w / 2), s.y - 17, std::ceil(w), 17}, {.size = ui_.gfx.classic_size(), .exact = true}, 0, {}, {}, 1);
}

void SoundTab::paint_popups(Painter& p, Rect area) {
  if (flows("settings_sound")) {
    for (std::size_t i = 0; i < volume_rows.size(); ++i) {
      if (row_shown(i)) picker_of_row(i)->box.paint_list(ui_, this, p, combo_rects_[i]);
    }
    reverb_.box.paint_list(ui_, this, p, combo_rects_[4]);
    return;
  }
  for (std::size_t i = 0; i < volume_rows.size(); ++i) {
    if (!row_shown(i)) continue;
    const Rect button{area.x + dropdown_x, area.y + volume_rows[i].dropdown_top, dropdown_w, dropdown_h};
    picker_of_row(i)->box.paint_list(ui_, this, p, button);
  }
  reverb_.box.paint_list(ui_, this, p, {area.x + dropdown_x, area.y + reverb_dropdown_top, dropdown_w, dropdown_h});
}

float SoundTab::content_height() const { return flows("settings_sound") ? flow_height_ : reverb_top + row_h + 25; }

void SoundTab::close_lists(const Picker* except) {
  speaker_.box.open = speaker_.box.open && &speaker_ == except;
  reverb_.box.open = reverb_.box.open && &reverb_ == except;
  for (Picker& picker : packs_) picker.box.open = picker.box.open && &picker == except;
}

void SoundTab::before_click(const Surface* owner, int id) {
  if (owner != this) {
    close_lists(nullptr);
    return;
  }
  const Picker* keep = nullptr;
  if (speaker_.box.owns(id)) keep = &speaker_;
  if (reverb_.box.owns(id)) keep = &reverb_;
  for (const Picker& picker : packs_) {
    if (picker.box.owns(id)) keep = &picker;
  }
  close_lists(keep);
}

void SoundTab::set_slider(int id, float window_x) {
  const float x = ui_.to_stage_x(window_x);
  if (flows("settings_sound")) {
    if (id == Balance) {
      const int value = slider_value(balance_rect_, x, balance_min, balance_max);
      if (value != ui_.prefs->get(pref::channel_balance)) ui_.prefs->set(pref::channel_balance, value);
      return;
    }
    const auto row = static_cast<std::size_t>(id - SpeakerVolume);
    const int value = slider_value(slider_rects_[row], x, 0, 100);
    if (value != ui_.prefs->get(volume_rows[row].key)) ui_.prefs->set(volume_rows[row].key, value);
    return;
  }
  if (id == Balance) {
    const float left = last_area_.x + balance_slider_x + balance_thumb_w / 2;
    const float at = std::clamp((x - left) / (balance_slider_w - balance_thumb_w), 0.0f, 1.0f);
    const int value = balance_min + static_cast<int>(at * static_cast<float>(balance_max - balance_min) + 0.5f);
    if (value != ui_.prefs->get(pref::channel_balance)) ui_.prefs->set(pref::channel_balance, value);
    return;
  }
  const pref::Int key = volume_rows[static_cast<std::size_t>(id - SpeakerVolume)].key;
  const int value = slider_value({last_area_.x + slider_x, 0, slider_w, slider_hit_h}, x, 0, 100);
  if (value != ui_.prefs->get(key)) ui_.prefs->set(key, value);
}

void SoundTab::pick(Picker& picker, int id) {
  if (id == picker.box.id_base && !picker.box.open && flows("settings_sound")) {
    const auto row = &picker == &reverb_ ? std::size_t{4} : &picker == &speaker_ ? std::size_t{0} : static_cast<std::size_t>(&picker - packs_.data()) + 1;
    place_dropdown(ui_, picker.box, combo_rects_[row]);
  } else if (id == picker.box.id_base && !picker.box.open) {
    // Opens upward only when there is more room there (main.js bindDropdowns).
    const auto row = &picker == &speaker_ ? std::size_t{0} : static_cast<std::size_t>(&picker - packs_.data()) + 1;
    const float top = last_area_.y + (&picker == &reverb_ ? reverb_dropdown_top : volume_rows[row].dropdown_top);
    const float list_h = std::min(static_cast<float>(picker.box.options.size()) * 36 + 12, 224.0f);
    const float below = view_bottom - (top + dropdown_h), above = top - view_top;
    picker.box.up = below < list_h + 8 && above > below;
  }
  if (!picker.box.click(id)) return;
  const auto index = static_cast<std::size_t>(picker.box.selected);
  if (index >= picker.ids.size()) return;
  const std::string& chosen = picker.ids[index];
  if (&picker == &speaker_) {
    ui_.prefs->set(pref::speaker_device_name, chosen.empty() ? std::string_view{} : std::string_view(picker.box.options[index]));
    ui_.prefs->set(pref::speaker_device, chosen);
    refresh_speakers();
    if (on_speaker_picked) on_speaker_picked();
  } else if (&picker == &reverb_) {
    ui_.prefs->set(pref::reverb, chosen);
  } else if (&picker == &packs_[0]) {
    ui_.prefs->set(pref::keyboard_sound, chosen);
  } else if (&picker == &packs_[1]) {
    ui_.prefs->set(pref::mouse_sound, chosen);
  } else {
    ui_.prefs->set(pref::pen_sound, chosen);
  }
}

bool SoundTab::acts_on_press(int id) const {
  if ((id >= SpeakerVolume && id <= PenVolume) || id == Balance) return true;
  if (id == speaker_.box.id_base || id == reverb_.box.id_base) return true;
  return std::ranges::any_of(packs_, [id](const Picker& picker) { return id == picker.box.id_base; });
}

void SoundTab::click(int id, float x, float, bool) {
  if ((id >= SpeakerVolume && id <= PenVolume) || id == Balance) {
    held_ = id;
    ui_.capture = this;
    set_slider(id, x);
    return;
  }
  for (const ToggleRow& row : toggle_rows) {
    if (row.id != id) continue;
    ui_.prefs->set(row.key, !ui_.prefs->get(row.key));
    knob_id_ = id;
    knob_start_ = clock_ms();
    return;
  }
  if (speaker_.box.owns(id)) {
    pick(speaker_, id);
    return;
  }
  if (reverb_.box.owns(id)) {
    pick(reverb_, id);
    return;
  }
  for (Picker& picker : packs_) {
    if (picker.box.owns(id)) {
      pick(picker, id);
      return;
    }
  }
}

void SoundTab::drag(float x, float) {
  if (held_ >= 0) set_slider(held_, x);
}

void SoundTab::release() { held_ = -1; }

bool SoundTab::wheel(float x, float y, float delta) {
  if (speaker_.box.wheel(ui_, x, y, delta) || reverb_.box.wheel(ui_, x, y, delta)) return true;
  return std::ranges::any_of(packs_, [&](Picker& picker) { return picker.box.wheel(ui_, x, y, delta); });
}

bool SoundTab::escape() {
  const bool any = speaker_.box.open || reverb_.box.open || std::ranges::any_of(packs_, [](const Picker& picker) { return picker.box.open; });
  close_lists(nullptr);
  return any;
}

bool SoundTab::animating() const {
  if (!ui_.animations) return false;
  const auto running = [&](unsigned long long start, float ms) {
    return start != 0 && ui_.now_ms < start + static_cast<unsigned long long>(ms);
  };
  return running(knob_start_, knob_ms) || running(tip_start_, tip_ms);
}

}  // namespace crpp::ui
