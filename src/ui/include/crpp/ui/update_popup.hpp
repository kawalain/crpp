#pragma once
// The card that offers a new version, on every view (.update-popup). It only shows what
// it is told; the shell does the work.
#include <functional>
#include <string>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

class UpdatePopup final : public Surface {
 public:
  using Surface::Surface;
  enum Id : int { Card, Button };
  enum class State : std::uint8_t { Hidden, Available, Busy, Failed };

  [[nodiscard]] static skin::ScreenSchema schema();

  // The button, in both of its wordings.
  std::function<void()> on_update;
  // The update check that was scheduled is due.
  std::function<void()> on_check_due;

  void show_available();
  // Up to 100, which then says it is installing.
  void show_progress(int percent);
  void show_failed();
  void hide() { state_ = State::Hidden; }
  [[nodiscard]] State state() const { return state_; }
  [[nodiscard]] int percent() const { return percent_; }

  // One wake-up, not a timer that keeps firing: this surface is on every view, so its
  // wake_at is what brings the app back for the next check. 0 cancels.
  void schedule_check(unsigned long long at_ms) { check_at_ = at_ms; }

  void paint(Painter& p);
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] Cursor cursor(int id) const override;
  bool tick(unsigned long long now_ms) override;
  [[nodiscard]] unsigned long long wake_at() const override { return check_at_; }

 private:
  State state_ = State::Hidden;
  int percent_ = 0;
  unsigned long long check_at_ = 0;
};

}  // namespace crpp::ui
