// The settings in the Luna Skin: an options window with its categories on the left and group
// boxes on the right. The tabs keep their state, hit ids and callbacks; only their rows flow
// here instead of standing where upstream's stylesheet puts them.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "crpp/ui/emoji_picker.hpp"
#include "crpp/ui/luna.hpp"
#include "crpp/ui/settings_panel.hpp"

namespace crpp::ui {

namespace {

// Rows going down the area, in group boxes.
class Form {
 public:
  Form(Ui& ui, Surface* owner, Painter& p, Rect area) : ui_(ui), owner_(owner), p_(p), t_(ui.theme), area_(area), y_(area.y + 10) {}

  void group(std::string_view legend) {
    end_group();
    legend_ = legend;
    group_top_ = y_;
    in_group_ = true;
    y_ += 14;
  }

  [[nodiscard]] float x() const { return area_.x + 12; }
  [[nodiscard]] float w() const { return area_.w - 24; }
  [[nodiscard]] float y() const { return y_; }
  void add(float h) { y_ += h; }

  void text(std::string_view line, Color ink) {
    const TextStyle style = luna::paragraph(p_);
    DWRITE_TEXT_METRICS m{};
    if (const auto layout = ui_.gfx.layout(widen(line), style, w())) layout->GetMetrics(&m);
    p_.text(line, {x(), y_, w(), m.height}, style, ink);
    y_ += m.height + 4;
  }

  void check(std::string_view label, bool on, int id, std::string_view hint = {}, bool off = false) {
    const Rect row{x(), y_, w(), luna::row_h};
    const bool hot = !off && ui_.hot(owner_, id);
    luna::check_box(p_, t_, {row.x, row.y + 3, luna::check_size, luna::check_size}, on, hot, off);
    p_.text(label, {row.x + 18, row.y, row.w - 18, row.h}, luna::body(p_, row.h), off ? t_.luna.off_ink : t_.luna.ink);
    if (!off) ui_.add_hit(owner_, row, id);
    y_ += row.h;
    if (!hint.empty()) {
      const TextStyle style = luna::paragraph(p_);
      DWRITE_TEXT_METRICS m{};
      if (const auto layout = ui_.gfx.layout(widen(hint), style, w() - 18)) layout->GetMetrics(&m);
      p_.text(hint, {row.x + 18, y_, w() - 18, m.height}, style, t_.luna.ink_soft);
      y_ += m.height + 4;
    }
  }

  // A label and a list on one line.
  Rect combo(std::string_view label, Dropdown& box) {
    const float list_w = std::min(170.0f, std::floor(w() * 0.55f));
    p_.text(label, {x(), y_, w() - list_w - 6, luna::field_h}, luna::body(p_, luna::field_h), t_.luna.ink);
    const Rect r{x() + w() - list_w, y_, list_w, luna::field_h};
    box.paint_button(ui_, owner_, p_, r);
    y_ += luna::field_h + 4;
    return r;
  }

  // A label, then a slider, with a list beside it if there is one.
  Rect slider(std::string_view label, int value, int min, int max, int id, Dropdown* box, Rect* box_rect, std::string_view value_word = {}, bool off = false) {
    p_.text(label, {x(), y_, w(), 16}, luna::body(p_, 16), t_.luna.ink);
    if (!value_word.empty()) p_.text(value_word, {x(), y_, box ? std::floor(w() * 0.42f) : w(), 16}, luna::body(p_, 16, Align::Right), t_.luna.ink_soft);
    y_ += 18;
    const float list_w = box ? std::floor(w() * 0.52f) : 0;
    const Rect r{x(), y_, w() - (box ? list_w + 8 : 0), luna::field_h};
    paint_slider(p_, t_, r, value, min, max);
    if (!off) ui_.add_hit(owner_, r, id);
    if (box) {
      *box_rect = {x() + w() - list_w, y_, list_w, luna::field_h};
      box->paint_button(ui_, owner_, p_, *box_rect);
    }
    y_ += luna::field_h + 6;
    return r;
  }

  void button(std::string_view label, int id) {
    const float bw = luna::button_width(p_, label);
    const Rect r{x() + w() - bw, y_, bw, luna::button_h};
    luna::button(p_, t_, r, label, {.hot = ui_.hot(owner_, id), .pressed = ui_.pressed(owner_, id)});
    ui_.add_hit(owner_, r, id);
    y_ += luna::button_h + 6;
  }

  // Closes the last box and says how tall it all was.
  float finish() {
    end_group();
    return y_ - area_.y;
  }

 private:
  void end_group() {
    if (!in_group_) return;
    y_ += 6;
    luna::group_box(p_, t_, {area_.x + 2, group_top_, area_.w - 4, y_ - group_top_}, legend_);
    y_ += 14;
    in_group_ = false;
  }

  Ui& ui_;
  Surface* owner_;
  Painter& p_;
  const Theme& t_;
  Rect area_;
  float y_;
  float group_top_ = 0;
  bool in_group_ = false;
  std::string_view legend_;
};

}  // namespace

void SettingsPanel::paint_luna(Painter& p) {
  if (!open_) return;
  const Theme& t = ui_.theme;
  const LunaColors& l = t.luna;
  const Rect c = ui_.content;
  p.set_base(D2D1::Matrix3x2F::Identity());
  p.fill_rect(c, l.face);
  ui_.add_hit_window(this, c, Overlay);

  constexpr float bottom_h = 36, list_w = 112, gap = 8;
  const Rect list{c.x + gap, c.y + gap, list_w, c.h - bottom_h - gap};
  luna::sunken(p, t, list);
  constexpr float row_h = 22;
  for (std::size_t i = 0; i < 5; ++i) {
    const int id = TabBase + static_cast<int>(i);
    const Rect row{list.x + 1, list.y + 1 + static_cast<float>(i) * row_h, list.w - 2, row_h};
    const bool active = static_cast<std::size_t>(page_) == i;
    if (active) p.fill_rect(row, l.select);
    else if (ui_.hot(this, id)) p.fill_rect(row, t.hl_pale2);
    p.text(tr().*tab_name(i), {row.x + 6, row.y, row.w - 12, row.h}, luna::body(p, row.h), active ? l.select_ink : l.ink);
    ui_.add_hit_window(this, row, id);
  }

  const Rect pane{list.right() + gap, c.y + gap, c.right() - gap - list.right() - gap, list.h};
  content_area_ = pane;
  ui_.dropdown_view = pane;
  SettingsTab& current = tab();
  if (current.content_height() <= 0) {  // only drawing a tab tells how tall it is
    const bool hits = ui_.hits_enabled;
    ui_.hits_enabled = false;
    p.rt()->PushAxisAlignedClip(D2D1::RectF(0, 0, 0, 0), D2D1_ANTIALIAS_MODE_ALIASED);
    current.paint(p, pane);
    p.rt()->PopAxisAlignedClip();
    ui_.hits_enabled = hits;
  }
  scroll_.view_h = pane.h;
  scroll_.content_h = current.content_height();
  const bool overflow = scroll_.content_h > pane.h;
  scroll_.clamp();
  scroll_.advance(ui_);
  const float area_w = pane.w - (overflow ? luna::scrollbar_w + 2 : 0);
  const Rect area{pane.x, pane.y - scroll_.drawn(), area_w, scroll_.content_h};
  ui_.push_hit_clip(pane);
  p.rt()->PushAxisAlignedClip(D2D1::RectF(pane.x, pane.y, pane.right(), pane.bottom()), D2D1_ANTIALIAS_MODE_ALIASED);
  current.paint(p, area);
  p.rt()->PopAxisAlignedClip();
  ui_.pop_hit_clip();
  if (current.content_height() != scroll_.content_h) ui_.more_frames = true;  // a row came or went
  scroll_.content_h = current.content_height();

  if (overflow) {
    const Rect bar{pane.right() - luna::scrollbar_w, pane.y, luna::scrollbar_w, pane.h};
    const auto parts = luna::scrollbar_parts(bar, scroll_.view_h, scroll_.content_h, scroll_.offset);
    scroll_track_ = parts.track;
    ui_.add_hit_window(this, bar, Track);
    ui_.add_hit_window(this, parts.thumb, Thumb);
    luna::scrollbar(p, t, parts, ui_.hot(this, Thumb), false, false);
  }
  current.paint_popups(p, area);

  const float bottom_y = c.bottom() - bottom_h + 7;
  const float close_w = luna::button_width(p, tr().options_close);
  const Rect close{c.right() - gap - close_w, bottom_y, close_w, luna::button_h};
  luna::button(p, t, close, tr().options_close, {.hot = ui_.hot(this, Droplet), .pressed = ui_.pressed(this, Droplet), .primary = true});
  ui_.add_hit_window(this, close, Droplet);
  if (!version_.empty()) {
    const Rect version{c.x + gap, bottom_y, 160, luna::button_h};
    luna::link(p, t, version, version_, ui_.hot(this, Version));
    ui_.add_hit_window(this, version, Version);
  }
}

void SoundTab::paint_luna(Painter& p, Rect area) {
  reverb_.box.options = {tr().sound_reverb_off, tr().sound_reverb_office, tr().sound_reverb_large};
  speaker_.box.options[0] = tr().sound_default_output;
  last_area_ = area;
  sync_selection();
  const Prefs& prefs = *ui_.prefs;
  Form form(ui_, this, p, area);
  const auto volume = [&](std::size_t row, const char* label, pref::Int key) {
    slider_rects_[row] = form.slider(label, prefs.get(key), 0, 100, SpeakerVolume + static_cast<int>(row), &picker_of_row(row)->box, &combo_rects_[row]);
  };
  form.group(tr().sound_speaker);
  volume(0, tr().sound_speaker_volume, pref::volume);
  form.group(tr().sound_devices);
  volume(1, tr().sound_keyboard_volume, pref::keyboard_volume);
  volume(2, tr().sound_mouse_volume, pref::mouse_volume);
  if (show_pen) volume(3, tr().sound_pen_volume, pref::pen_volume);
  form.check(tr().sound_listen_self, prefs.get(pref::listen_self), ListenSelf);
  form.check(tr().sound_others_use_mine, prefs.get(pref::others_use_my_sounds), OthersUseMine);
  form.check(tr().sound_mute_notifications, prefs.get(pref::mute_notifications_on_mute), MuteNotifications);
  form.check(tr().sound_mute_on_idle, prefs.get(pref::mute_on_idle), MuteOnIdle);
  form.group(tr().sound_effects);
  const bool spatial = prefs.get(pref::spatial_audio);
  form.check(tr().sound_spatial, spatial, Spatial);
  form.check(tr().sound_balance_enabled, prefs.get(pref::channel_balance_enabled), BalanceEnabled, {}, !spatial);
  const bool on = spatial && prefs.get(pref::channel_balance_enabled);
  const int value = std::clamp(prefs.get(pref::channel_balance), 55, 90);
  balance_rect_ = form.slider(tr().sound_balance, value, 55, 90, Balance, nullptr, nullptr, on ? balance_word(value) : std::string_view(tr().sound_balance_off), !on);
  form.text(tr().sound_balance_hint, ui_.theme.luna.ink_soft);
  combo_rects_[4] = form.combo(tr().sound_reverb, reverb_.box);
  flow_height_ = form.finish();
  paint_tip(p, area);
}

void DisplayTab::paint_luna(Painter& p, Rect area) {
  const Theme& t = ui_.theme;
  const Prefs& prefs = *ui_.prefs;
  const bool sync = prefs.get(pref::system_theme);
  const auto every = all_theme_entries(), light = themes_of(false), dark = themes_of(true);
  const auto fill = [](Dropdown& box, const std::vector<const ThemeEntry*>& list, const std::string& saved) {
    const std::string_view id = theme_from_upstream(saved);
    box.options.clear();
    box.selected = 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
      box.options.emplace_back(tr().*list[i]->label);
      if (list[i]->id == id) box.selected = static_cast<int>(i);
    }
  };
  fill(theme_, every, prefs.get(pref::theme));
  fill(light_, light, prefs.get(pref::light_theme));
  fill(dark_, dark, prefs.get(pref::dark_theme));

  Form form(ui_, this, p, area);
  form.group(tr().display_theme);
  form.check(tr().display_sync, sync, SystemTheme);
  if (sync) {
    list_rect_[1] = form.combo(tr().display_light, light_);
    list_rect_[2] = form.combo(tr().display_dark, dark_);
  } else {
    list_rect_[0] = form.combo(tr().display_general, theme_);
  }
  form.group(tr().room_word_chat);
  const int percent = std::clamp(prefs.get(pref::chat_scale), 80, 150);
  const float sample_size = std::round(ui_.gfx.classic_size() * static_cast<float>(percent) / 100);
  const Rect sample{form.x(), form.y(), form.w(), 2 * (sample_size + 4) + 12};
  luna::sunken(p, t, sample);
  const TextStyle name{.size = sample_size, .line_height = sample_size + 4, .ellipsis = true, .exact = true};
  const std::string nick = sample_nick_.empty() ? std::string(tr().guest) : sample_nick_;
  p.text(locale::fill(tr().chat_says, {nick}), {sample.x + 6, sample.y + 5, sample.w - 12, name.line_height}, name, t.luna.ink_soft);
  p.text(tr().display_chat_sample, {sample.x + 18, sample.y + 6 + name.line_height, sample.w - 24, name.line_height}, name, t.luna.ink);
  form.add(sample.h + 8);
  slider_ = form.slider(tr().display_chat_size, percent, 80, 150, ScaleSlider, nullptr, nullptr, std::to_string(percent) + "%");
  form.group(tr().system_time);
  form.check(tr().system_24h, prefs.get(pref::use_24h), Use24h);
  sync_idle();
  idle_rect_ = form.combo(tr().system_idle, idle_);
  form.group(tr().system_language);
  sync_language();
  language_rect_ = form.combo(tr().system_language_row, language_);
  flow_height_ = form.finish();
}

void NotifyTab::paint_luna(Painter& p, Rect area) {
  const Prefs& prefs = *ui_.prefs;
  Form form(ui_, this, p, area);
  form.group(tr().notify_on_message);
  form.check(tr().notify_tray, prefs.get(pref::chat_tray_notify), TrayToggle);
  form.check(tr().notify_flash, prefs.get(pref::chat_taskbar_flash), FlashToggle);
  form.group(tr().notify_sounds);
  for (std::size_t i = 0; i < sounds_.size(); ++i) {
    const float top = form.y();
    form.check(sounds_[i].name, prefs.notification_sound(sounds_[i].id), SoundBase + static_cast<int>(i));
    const char* preview = tr().notify_preview;
    const float w = luna::button_width(p, preview);
    const Rect r{form.x() + form.w() - w, top - 1, w, luna::button_h - 1};
    const int id = PreviewBase + static_cast<int>(i);
    luna::button(p, ui_.theme, r, preview, {.hot = ui_.hot(this, id), .pressed = ui_.pressed(this, id)});
    ui_.add_hit(this, r, id);
    form.add(3);
  }
  flow_height_ = form.finish();
}

void SystemTab::paint_luna(Painter& p, Rect area) {
  const Prefs& prefs = *ui_.prefs;
  Form form(ui_, this, p, area);
  form.group(tr().system_heading);
  form.check(tr().system_autostart, prefs.get(pref::start_with_system), Autostart);
  form.check(tr().system_tray, prefs.get(pref::minimize_to_tray), Tray);
  form.check(tr().system_detail, prefs.get(pref::input_detail), Detail, tr().system_detail_hint);
  form.group(tr().system_advanced);
  sync_release();
  release_rect_ = form.combo(tr().system_release, release_);
  const bool logging = prefs.get(pref::log_enabled);
  form.check(tr().system_logging, logging, Logging);
  if (logging) form.button(tr().system_open_logs, OpenLogs);
  form.button(tr().system_reset, Reset);
  flow_height_ = form.finish();
}

void AccountTab::paint_luna(Painter& p, Rect area) {
  const Theme& t = ui_.theme;
  const LunaColors& l = t.luna;
  Form form(ui_, this, p, area);
  form.group(tr().account_heading);
  const Rect icon{form.x(), form.y(), 64, 64};
  luna::picture_frame(p, t, icon, ui_.hot(this, Emoji) ? 1.0f : 0.0f);
  draw_svg(p, ui_.emoji_image(profile_.emoji), {icon.x + 6, icon.y + 6, 52, 52});
  ui_.add_hit(this, icon, Emoji);
  const float names_x = icon.right() + 10, names_w = form.x() + form.w() - names_x;
  if (!profile_.nickname.empty() && !profile_.handle.empty()) {
    p.text(profile_.nickname, {names_x, icon.y + 14, names_w, 18}, luna::bold(p, 18), l.ink);
    p.text("@" + profile_.handle, {names_x, icon.y + 34, names_w, 16}, luna::body(p, 16), l.ink_soft);
  } else {
    p.text(profile_.display, {names_x, icon.y + 23, names_w, 18}, luna::bold(p, 18), l.ink);
  }
  form.add(64 + 10);
  float x = form.x();
  const float y = form.y();
  const auto button = [&](std::string_view label, int id) {
    const float w = luna::button_width(p, label);
    const Rect r{x, y, w, luna::button_h};
    luna::button(p, t, r, label, {.hot = ui_.hot(this, id), .pressed = ui_.pressed(this, id)});
    ui_.add_hit(this, r, id);
    x = r.right() + 6;
  };
  button(tr().account_change_nickname, Nickname);
  if (profile_.has_account) button(tr().account_change_handle, Handle);
  form.add(luna::button_h + 8);
  form.button(tr().account_logout, Logout);
  flow_height_ = form.finish();
}

}  // namespace crpp::ui
