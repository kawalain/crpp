#pragma once
// A CSS transition between two states, and the hover label (.tip) many controls show.
#include <algorithm>
#include <string_view>

#include "crpp/ui/controls.hpp"

namespace crpp::ui {

// Runs from 0 (off) to 1 (on) over duration_ms, eased. Turning around midway starts
// from where it is and takes proportionally less time.
class Tween {
 public:
  explicit constexpr Tween(float duration_ms) : duration_ms_(duration_ms) {}

  void aim(bool on, unsigned long long now, bool animate) {
    if (!animate) {
      on_ = on;
      from_ = on ? 1.0f : 0.0f;
      span_ms_ = 0;
      return;
    }
    if (on == on_) return;
    from_ = value(now);
    on_ = on;
    start_ = now;
    span_ms_ = duration_ms_ * (on ? 1 - from_ : from_);
  }

  [[nodiscard]] float value(unsigned long long now) const {
    const float to = on_ ? 1.0f : 0.0f;
    if (span_ms_ <= 0) return to;
    return from_ + (to - from_) * ease(static_cast<float>(now - start_) / span_ms_);
  }

  // True while it has not reached `on`, including when it was not aimed there yet.
  [[nodiscard]] bool moving(bool on, unsigned long long now) const {
    return on != on_ || (span_ms_ > 0 && static_cast<float>(now - start_) < span_ms_);
  }

 private:
  float duration_ms_;
  float from_ = 0, span_ms_ = 0;
  unsigned long long start_ = 0;
  bool on_ = false;
};

inline constexpr float tip_ms = 150;  // .tip transition
inline constexpr float tip_pad_x = 15, tip_pad_y = 5, tip_line = 22;
inline constexpr TextStyle tip_style{.size = 16, .weight = 700, .line_height = tip_line, .align = Align::Center};

[[nodiscard]] inline float tip_width(const Ui& ui, std::string_view text) {
  DWRITE_TEXT_METRICS m{};
  if (const auto layout = ui.gfx.layout(widen(text), tip_style, 600)) layout->GetMetrics(&m);
  return m.widthIncludingTrailingWhitespace + 2 * tip_pad_x;
}
[[nodiscard]] constexpr float tip_height() { return tip_line + 2 * tip_pad_y; }

inline void paint_tip(Painter& p, Rect r, std::string_view text, Color fill, Color ink, float shown) {
  p.fill_round(r, 11, with_alpha(fill, shown));
  p.text(text, {r.x + tip_pad_x, r.y + tip_pad_y, r.w - 2 * tip_pad_x + 1, tip_line}, tip_style, with_alpha(ink, shown));
}

}  // namespace crpp::ui
