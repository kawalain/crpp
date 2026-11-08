#include "crpp/ui/member_menu.hpp"

#include <algorithm>
#include <string>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum TextId : int { ProfileText, VolumeText, MuteText };
enum NumberId : int { Self, Muted, Volume, PointX, PointY };
constexpr skin::Name text_names[] = {{"profile", ProfileText}, {"volume_label", VolumeText}, {"mute", MuteText}};
constexpr skin::Name number_names[] = {{"self", Self}, {"muted", Muted}, {"volume", Volume}, {"point_x", PointX}, {"point_y", PointY}};
constexpr skin::Name hit_names[] = {{"card", MemberMenu::Card}, {"profile", MemberMenu::Profile}, {"slider", MemberMenu::Slider}, {"mute", MemberMenu::Mute}};
constexpr skin::Name note_names[] = {{"track", 0}};

}  // namespace

skin::ScreenSchema MemberMenu::schema() { return {.screen = "member_menu", .texts = text_names, .numbers = number_names, .hits = hit_names, .notes = note_names}; }

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
  if (const skin::Screen* screen = ui_.pack_screen("member_menu")) paint_pack(p, *screen);
}

HoverTip::Spec MemberMenu::volume_tip() const {
  // .volume-tooltip, over the thumb.
  constexpr float thumb = 21;
  const float at = static_cast<float>(volume_) / 100.0f;
  HoverTip::Spec tip;
  tip.text = std::to_string(volume_) + "%";
  tip.anchor = {track_.x + thumb / 2 + (track_.w - thumb) * at, track_.y, 0, track_.h};
  tip.look = HoverTip::Look::Volume;
  tip.gap = 11;
  return tip;
}

void MemberMenu::paint_pack(Painter& p, const skin::Screen& screen) {
  class Shown final : public skin::Source {
   public:
    explicit Shown(MemberMenu& menu) : m_(menu) {}
    std::string_view text(int id, int) override {
      switch (id) {
        case ProfileText: return tr().member_profile;
        case VolumeText: return tr().member_volume;
        default: return m_.muted() ? tr().unmute : tr().mute;
      }
    }
    float number(int id, int) override {
      switch (id) {
        case Self: return m_.self_ ? 1.0f : 0.0f;
        case Muted: return m_.muted() ? 1.0f : 0.0f;
        case Volume: return static_cast<float>(m_.volume_);
        case PointX: return m_.x_;
        default: return m_.y_;
      }
    }
    void note(int, int, skin::Box box) override { m_.track_ = rect_of(box); }

   private:
    MemberMenu& m_;
  } shown(*this);
  draw_layer(ui_, p, this, screen, {0, 0, ui_.width, ui_.height}, shown);
  const bool show_tip = !self_ && (ui_.hot(this, Slider) || ui_.capture == this);
  const HoverTip::Spec tip = show_tip ? volume_tip() : HoverTip::Spec{};
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
