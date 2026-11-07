#include "crpp/ui/member_menu.hpp"

#include <algorithm>
#include <string>

#include "crpp/ui/controls.hpp"

namespace crpp::ui {

namespace {

constexpr float menu_pad = 6, menu_item_h = 38;
constexpr float menu_row_h = 6 + 22 + 8 + 14 + 10;  // .menu-volume: padding, label, margin, slider
// .member-menu's min-width; one with a volume row is a fifth wider, for the slider.
constexpr float self_w = 128, other_w = 154;

}  // namespace

void MemberMenu::open(std::uint32_t slot, bool self, int volume, float x, float y) {
  open_ = true;
  self_ = self;
  slot_ = slot;
  volume_ = volume;
  x_ = x;
  y_ = y;
  window_w_ = window_h_ = 0;
}

void MemberMenu::close() {
  open_ = false;
  if (ui_.capture == this) ui_.capture = nullptr;
}

void MemberMenu::paint(Painter& p) {
  if (!open_) return;
  if (window_w_ != 0 && (window_w_ != ui_.width || window_h_ != ui_.height)) {
    close();
    return;
  }
  window_w_ = ui_.width;
  window_h_ = ui_.height;
  const Theme& t = ui_.theme;
  const bool plain = !ui_.skin().moves();
  const float pad = plain ? 1 : menu_pad, item_h = plain ? 22 : menu_item_h, row_h = plain ? 46 : menu_row_h;
  const float w = self_ ? self_w : other_w;
  const float h = 2 * pad + 2 * item_h + (self_ ? 0 : row_h);
  const Rect r{std::max(8.0f, std::min(x_, ui_.width - w - 8)), std::max(8.0f, std::min(y_, ui_.height - h - 8)), w, h};
  ui_.skin().panel(p, t, r, PanelKind::Menu);
  ui_.add_hit_window(this, r, Card);

  const auto item = [&](Rect box, std::string_view text, int id, bool active, bool disabled) {
    ui_.skin().menu_item(p, t, box, text, active, !disabled && ui_.hot(this, id), disabled);
    if (!disabled) ui_.add_hit_window(this, box, id);
  };

  float y = r.y + pad;
  item({r.x + pad, y, w - 2 * pad, item_h}, tr().member_profile, Profile, false, false);
  y += item_h;
  HoverTip::Spec tip;
  bool show_tip = false;
  if (!self_) {
    p.text(tr().member_volume, {r.x + pad + 12, y + 6, w - 2 * pad - 24, 22}, {.size = 17, .weight = 700, .line_height = 22}, t.hl_text);
    track_ = {r.x + pad + 12, y + 6 + 22 + 8, w - 2 * pad - 24, 14};
    paint_slider(p, t, track_, volume_, 0, 100);
    ui_.add_hit_window(this, track_, Slider);
    y += row_h;
    show_tip = ui_.hot(this, Slider) || ui_.capture == this;
    if (show_tip) {
      // .volume-tooltip, over the thumb.
      constexpr float thumb = 21;
      const float at = static_cast<float>(volume_) / 100.0f;
      tip.text = std::to_string(volume_) + "%";
      tip.anchor = {track_.x + thumb / 2 + (track_.w - thumb) * at, track_.y, 0, track_.h};
      tip.size = 15;
      tip.weight = 400;
      tip.line_height = 20;
      tip.gap = 11;
      tip.slide = 0;
      tip.in_ms = tip.out_ms = 120;
      tip.pad_x = 9;
      tip.pad_y = 3;
      tip.radius = 13;
      tip.fill = rgb(0x75ccff);
      tip.ink = rgb(0xFFFFFF);
    }
  }
  const bool muted_now = muted();
  item({r.x + pad, y, w - 2 * pad, item_h}, muted_now ? tr().unmute : tr().mute, Mute, muted_now, self_);
  tooltip_.paint(ui_, p, show_tip ? &tip : nullptr);
}

void MemberMenu::set_volume(int percent) {
  volume_ = percent;
  if (on_volume) on_volume(slot_, percent);
}

void MemberMenu::before_click(const Surface* owner, int) {
  if (open_ && owner != this) close();
}

void MemberMenu::click(int id, float x, float, bool) {
  if (id == Profile) {
    const auto slot = slot_;
    close();
    if (on_profile) on_profile(slot);
  } else if (id == Mute && !self_) {
    // Mute is 0%, unmute is back to 100%; the menu stays for another change.
    set_volume(volume_ == 0 ? 100 : 0);
  } else if (id == Slider) {
    ui_.capture = this;
    set_volume(slider_value(track_, x, 0, 100));
  }
}

void MemberMenu::drag(float x, float) { set_volume(slider_value(track_, x, 0, 100)); }

bool MemberMenu::escape() {
  if (!open_) return false;
  close();
  return true;
}

Cursor MemberMenu::cursor(int id) const { return id == Card ? Cursor::Arrow : Cursor::Hand; }

}  // namespace crpp::ui
