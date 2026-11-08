#include "crpp/ui/settings_rows.hpp"

#include <algorithm>

namespace crpp::ui {

float text_width(const Painter& p, std::string_view text, const TextStyle& style) {
  const auto layout = p.gfx().layout(widen(text), style, 2000);
  DWRITE_TEXT_METRICS m{};
  if (layout) layout->GetMetrics(&m);
  return m.width;
}

void paint_bubble(Painter& p, const Theme& t, std::string_view text, Rect box, const TextStyle& style, float radius, Color fill, Color ink,
                  float alpha) {
  const Color written = skin_of(t).bubble(p, t, box, radius, fill, ink, alpha);
  TextStyle centered = style;
  centered.align = Align::Center;
  centered.line_height = box.h;
  p.text(text, box, centered, written);
}

void place_dropdown(const Ui& ui, Dropdown& dropdown, Rect button) {
  const auto n = static_cast<float>(dropdown.options.size());
  const auto [pad, item_h, gap, max_h, margin] = ui.skin().list_metrics();
  const float list_h = std::min(max_h, n * item_h + (n - 1) * gap + 2 * pad);
  const float below = ui.dropdown_view.bottom() - button.bottom();
  const float above = button.y - ui.dropdown_view.y;
  dropdown.up = below < list_h + 8 && above > below;
}

}  // namespace crpp::ui
