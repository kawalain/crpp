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

  std::function<void(std::uint32_t slot)> on_profile;
  std::function<void(std::uint32_t slot, int percent)> on_volume;

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
  void drag(float x, float y) override;
  bool escape() override;
  [[nodiscard]] Cursor cursor(int id) const override;
  [[nodiscard]] bool animating() const override { return open_ && tooltip_.animating(ui_); }

 private:
  void set_volume(int percent);

  bool open_ = false, self_ = false;
  std::uint32_t slot_ = 0;
  int volume_ = 100;
  float x_ = 0, y_ = 0;
  float window_w_ = 0, window_h_ = 0;  // a resize closes it
  Rect track_;
  HoverTip tooltip_;
};

}  // namespace crpp::ui
