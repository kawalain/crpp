#pragma once
// The popup while the server cannot be reached (.offline-backdrop): it cannot be
// dismissed, it goes away when the connection is back. Only the settings droplet stays
// usable above it, and open settings cover it. It also keeps the time of the next
// reachability check, so that waiting for one needs a wake-up and no timer of its own.
#include <functional>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

class OfflinePopup final : public Surface {
 public:
  using Surface::Surface;
  enum Id : int { Backdrop, Card };
  [[nodiscard]] bool holds_keys(int id) const override { return id == Backdrop; }

  [[nodiscard]] static skin::ScreenSchema schema();

  // The next check is due; runs inside tick().
  std::move_only_function<void() const> on_retry;

  void show();
  void hide();
  [[nodiscard]] bool is_shown() const { return shown_; }
  void set_covered(bool covered) { covered_ = covered; }
  // on_retry runs at this time; it survives the settings being open.
  void retry_at(unsigned long long ms) { retry_at_ = ms; }

  // What the message takes inside a window of its own.
  [[nodiscard]] D2D1_SIZE_F content_size() const;
  void paint(Painter& p);
  bool wheel(float x, float y, float delta) override;
  [[nodiscard]] Cursor cursor(int id) const override;
  bool tick(unsigned long long now_ms) override;
  [[nodiscard]] unsigned long long wake_at() const override { return retry_at_; }

 private:
  bool shown_ = false;
  bool covered_ = false;
  bool was_animating_ = false;
  bool settled_ = false;  // it has risen once, so coming out from under the settings does not rise again
  unsigned long long opened_at_ = 0;
  unsigned long long retry_at_ = 0;  // 0 = none
};

}  // namespace crpp::ui
