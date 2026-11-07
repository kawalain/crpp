#include "crpp/ui/setup_screen.hpp"

#include <algorithm>

namespace crpp::ui {

void SetupScreen::paint(Painter& p) {
  const Theme& t = ui_.theme;
  p.set_base(D2D1::Matrix3x2F::Identity());
  const float w = std::min(380.0f, ui_.width - 32);
  const float x = (ui_.width - w) / 2;
  const float y = ui_.height / 2 - 60;
  const Color ink = t.ink_strong;
  const TextStyle message{.size = 18, .weight = 700, .line_height = 26, .align = Align::Center, .wrap = true};
  p.text(std::string_view(tr().setup_message), {x, y, w, 26}, message, ink);
  const Rect track{x, y + 46, w, 8};
  ui_.skin().progress(p, t, track, percent_);
  const TextStyle percent{.size = 15, .line_height = 22, .align = Align::Center};
  p.text(std::to_string(percent_) + "%", {x, y + 64, w, 22}, percent, ink);
}

}  // namespace crpp::ui
