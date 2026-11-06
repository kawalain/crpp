#include "crpp/ui/settings_rows.hpp"

#include <algorithm>

namespace crpp::ui {

void Fade::set(const Ui& ui, bool on, float ms_on, float ms_off) {
  if (!started_) {
    started_ = true;
    on_ = on;
    from_ = on ? 1.0f : 0.0f;
    return;
  }
  if (on == on_) return;
  from_ = value(ui);
  on_ = on;
  ms_ = on ? ms_on : ms_off;
  start_ = ui.now_ms;
}

float Fade::value(const Ui& ui) const {
  const float target = on_ ? 1.0f : 0.0f;
  if (start_ == 0) return target;
  return from_ + (target - from_) * transition(ui, start_, ms_);
}

bool Fade::running(const Ui& ui) const {
  return ui.animations && start_ != 0 && static_cast<float>(ui.now_ms - start_) < ms_;
}

void paint_heading(Painter& p, const Theme& t, Rect area, float top, std::string_view text) {
  p.text(text, {area.x + 9, area.y + top, area.w - 9, 32}, {.size = 26, .weight = 700, .line_height = 32}, t.hl_text);
}

void paint_subheading(Painter& p, const Theme& t, Rect area, float top, std::string_view text) {
  p.text(text, {area.x + 12, area.y + top, area.w - 12, 28}, {.size = 21, .weight = 700, .line_height = 28},
         t.dark ? t.hl_text_dark : t.hl);
}

void paint_row_label(Painter& p, const Theme& t, Rect area, float top, std::string_view text) {
  p.text(text, {area.x + 16, area.y + top, 300, 26}, {.size = 18, .line_height = 26}, t.hl_text);
}

void paint_toggle(Ui& ui, Surface* owner, Painter& p, Rect area, float top, std::string_view label, bool on, int id,
                  ToggleAnim& anim, std::string_view hint) {
  const Theme& t = ui.theme;
  const float x = area.x + 16, y = area.y + top;
  constexpr float row_w = 505, switch_w = 50, switch_h = 26;
  p.text(label, {x, y, row_w - switch_w - 16, 36}, {.size = 18, .line_height = 36}, t.hl_text);
  if (!hint.empty()) {
    p.text(hint, {x, y + 28, row_w - switch_w - 16, 60}, {.size = 14, .line_height = 20, .wrap = true}, t.hl_text_soft);
  }

  anim.knob.set(ui, on, 160);
  anim.hover.set(ui, ui.hot(owner, id), 150);
  const float at = anim.knob.value(ui);
  const Rect r{x + row_w - switch_w, y + 5, switch_w, switch_h};
  const Color off = t.dark ? t.hl_pale2 : rgb(0xcddfea);
  p.fill_round(r, 13, mix(t.hl, off, at * 100));
  if (const float shade = anim.hover.value(ui); shade > 0) p.fill_round(r, 13, rgb(0x000000, 0.05f * shade));
  const float cx = r.x + 13 + 24 * at, cy = r.y + 13;
  // box-shadow: 0 1px 3px, with #0005 in the dark theme and #608ba355 otherwise.
  const Color shadow = t.dark ? rgb(0x000000, 0.33f) : rgb(0x608ba3, 0.33f);
  p.fill_ellipse(cx, cy + 1, 11, 11, with_alpha(shadow, shadow.a * 0.5f));
  p.fill_ellipse(cx, cy + 1, 10.5f, 10.5f, shadow);
  p.fill_ellipse(cx, cy, 10, 10, t.dark ? mix(t.on_accent, t.hl_text_soft, at * 100) : t.surface);
  ui.add_hit(owner, r, id);
}

void paint_text_button(Ui& ui, Surface* owner, Painter& p, Rect r, std::string_view label, int id, Fade& hover) {
  const Theme& t = ui.theme;
  hover.set(ui, ui.hot(owner, id), 150);
  const float at = hover.value(ui);
  if (at > 0) p.push(scale_about(r.x + r.w / 2, r.y + r.h / 2, 1 + 0.03f * at));
  p.fill_round(r, 8, mix(t.hl_dark, t.hl, at * 100));
  p.text(label, r, {.size = 16, .line_height = r.h, .align = Align::Center}, t.on_accent);
  if (at > 0) p.pop();
  ui.add_hit(owner, r, id);
}

float text_width(const Painter& p, std::string_view text, const TextStyle& style) {
  const auto layout = p.gfx().layout(widen(text), style, 2000);
  DWRITE_TEXT_METRICS m{};
  if (layout) layout->GetMetrics(&m);
  return m.width;
}

void paint_bubble(Painter& p, std::string_view text, Rect box, const TextStyle& style, float radius, Color fill, Color ink,
                  float alpha) {
  p.fill_round(box, radius, with_alpha(fill, fill.a * alpha));
  TextStyle centered = style;
  centered.align = Align::Center;
  centered.line_height = box.h;
  p.text(text, box, centered, with_alpha(ink, ink.a * alpha));
}

void place_dropdown(Dropdown& dropdown, Rect button) {
  const auto n = static_cast<float>(dropdown.options.size());
  const float list_h = std::min(224.0f, n * 38 + (n - 1) * 4 + 12);
  const float below = settings_view.bottom() - button.bottom();
  const float above = button.y - settings_view.y;
  dropdown.up = below < list_h + 8 && above > below;
}

}  // namespace crpp::ui
