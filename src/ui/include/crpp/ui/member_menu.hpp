#pragma once
// What right-clicking a member opens: their profile, and how loud they are here.
#include <cstdint>
#include <functional>

#include "crpp/ui/surface.hpp"
#include "crpp/ui/tip.hpp"

namespace crpp::ui {

class MemberMenu final : public Surface {
 public:
  using Surface::Surface;
  enum Id : int { Card, Profile, Slider, Mute };
  [[nodiscard]] std::optional<Role> role(int id) const override {
    return id == Profile ? std::optional(Role::Button) : id == Slider ? std::optional(Role::Slider) : id == Mute ? std::optional(Role::Toggle) : std::nullopt;
  }
  [[nodiscard]] bool holds_keys(int id) const override { return id == Card; }
  bool stop_key(int id, unsigned vk, bool ctrl, bool shift, bool alt) override;

  std::move_only_function<void(std::uint32_t slot) const> on_profile;
  std::move_only_function<void(std::uint32_t slot, int percent) const> on_volume;

  [[nodiscard]] static skin::ScreenSchema schema();

  // x, y in window coordinates; it keeps 8 away from the edges. One's own menu has no
  // volume, and its mute is disabled.
  void open(std::uint32_t slot, bool self, int volume, float x, float y);
  void close();
  [[nodiscard]] bool is_open() const { return open_; }
  [[nodiscard]] std::uint32_t slot() const { return slot_; }
  [[nodiscard]] int volume() const { return volume_; }
  [[nodiscard]] bool muted() const { return !self_ && volume_ == 0; }

  void paint(Painter& p);
  void before_click(const Surface* owner, int id) override;
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] bool acts_on_press(int id) const override { return id == Slider; }
  void drag(float x, float y) override;
  bool escape() override;
  [[nodiscard]] Cursor cursor(int id) const override;
  [[nodiscard]] bool animating() const override { return open_ && tooltip_.animating(ui_); }

 private:
  void set_volume(int percent);
  void paint_pack(Painter& p, const skin::Screen& screen);
  [[nodiscard]] HoverTip::Spec volume_tip() const;

  bool open_ = false, self_ = false;
  std::uint32_t slot_ = 0;
  int volume_ = 100;
  float x_ = 0, y_ = 0;
  float window_w_ = 0, window_h_ = 0;  // a resize closes it
  Rect track_;
  HoverTip tooltip_;
};

}  // namespace crpp::ui
