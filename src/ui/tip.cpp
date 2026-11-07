#include "crpp/ui/tip.hpp"

#include <algorithm>

#include "crpp/ui/controls.hpp"

namespace crpp::ui {

float keyframes(float t, std::span<const std::pair<float, float>> stops) {
  for (std::size_t i = 1; i < stops.size(); ++i) {
    if (t < stops[i].first) {
      const auto& [from_at, from] = stops[i - 1];
      const auto& [to_at, to] = stops[i];
      return from + (to - from) * ease((t - from_at) / (to_at - from_at));
    }
  }
  return stops.empty() ? 0 : stops.back().second;
}

void HoverTip::paint(Ui& ui, Painter& p, const Spec* want) {
  seen_owner_ = ui.hover_owner;
  seen_id_ = ui.hover_id;
  if (want) {
    if (!shown_ || want->text != spec_.text) since_ = ui.now_ms;
    shown_ = true;
    spec_ = *want;
  } else if (shown_) {
    shown_ = false;
    since_ = ui.now_ms;
  }
  if (spec_.text.empty()) return;

  const float moved = transition(ui, since_, shown_ ? spec_.in_ms : spec_.out_ms);
  const float alpha = shown_ ? moved : 1 - moved;
  if (alpha <= 0) return;

  const TextStyle style{.size = spec_.size,
                        .weight = spec_.weight,
                        .line_height = spec_.line_height,
                        .align = Align::Center,
                        .wrap = true};
  DWRITE_TEXT_METRICS m{};
  TextStyle measure = style;
  measure.align = Align::Left;
  measure.wrap = false;
  if (const auto layout = ui.gfx.layout(widen(spec_.text), measure, 2000)) layout->GetMetrics(&m);
  const float w = m.widthIncludingTrailingWhitespace + 2 * spec_.pad_x + 1;
  const float h = m.height + 2 * spec_.pad_y;

  float x = spec_.anchor.x;
  if (spec_.place == Place::Center) x += (spec_.anchor.w - w) / 2;
  if (spec_.place == Place::Right) x = spec_.anchor.right() - w;
  x = std::max(4.0f, std::min(x, ui.width - w - 4)) + spec_.dx;
  const float y = spec_.anchor.y - spec_.gap - h + spec_.slide * (1 - alpha);

  const Theme& t = ui.theme;
  const Color fill = spec_.fill.value_or(t.hl);
  const Color ink = spec_.ink.value_or(t.on_accent);
  const Color written = ui.skin().bubble(p, t, {x, y, w, h}, spec_.radius, fill, ink, alpha);
  p.text(spec_.text, {x + spec_.pad_x, y + spec_.pad_y, w - 2 * spec_.pad_x, h - 2 * spec_.pad_y}, style, written);
}

bool HoverTip::animating(const Ui& ui) const {
  if (!ui.animations) return false;
  if (ui.hover_owner != seen_owner_ || ui.hover_id != seen_id_) return true;
  return static_cast<float>(ui.now_ms - std::min(ui.now_ms, since_)) < (shown_ ? spec_.in_ms : spec_.out_ms) && !spec_.text.empty();
}

}  // namespace crpp::ui
