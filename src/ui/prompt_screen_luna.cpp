// The account prompt in the Luna Skin: a small sign-in window. It shares its state, hit ids and
// callbacks with the Cloud layout.
#include <algorithm>
#include <cmath>

#include "crpp/ui/luna.hpp"
#include "crpp/ui/prompt_screen.hpp"

namespace crpp::ui {

void PromptScreen::paint_luna(Painter& p, bool is_locked, bool hint_error) {
  const Theme& t = ui_.theme;
  const LunaColors& l = t.luna;
  const Rect c = ui_.content;
  p.set_base(D2D1::Matrix3x2F::Identity());
  p.fill_rect(c, l.face);

  const float w = std::min(300.0f, c.w - 24);
  const float left = std::floor(c.x + (c.w - w) / 2);
  const TextStyle hint_style = luna::paragraph(p);
  float hint_h = 16;
  if (const auto layout = ui_.gfx.layout(widen(hint_shown_), hint_style, w)) {
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    hint_h = std::max(hint_h, m.height);
  }
  float h = 64 + 12 + luna::field_h + 6 + hint_h + 14 + luna::button_h;
  if (!prompt_.heading.empty()) h += 26;
  if (!prompt_.label.empty()) h += 18;
  if (!prompt_.link.empty()) h += 26;
  float y = std::floor(c.y + std::max(8.0f, (c.h - h) / 2));

  const Rect picture{std::floor(c.x + (c.w - 64) / 2), y, 64, 64};
  luna::picture_frame(p, t, picture, 0);
  draw_svg(p, ui_.emoji_image(""), {picture.x + 6, picture.y + 6, 52, 52});
  y += 64 + 12;

  if (!prompt_.heading.empty()) {
    p.text(prompt_.heading, {left, y, w, 20}, luna::bold(p, 20, Align::Center), l.ink);
    y += 26;
  }
  if (!prompt_.label.empty()) {
    p.text(prompt_.label, {left, y, w, 16}, luna::body(p, 16), l.ink);
    y += 18;
  }

  // The field, with what goes before the text and the eye inside it.
  const Rect field{left, y, w, luna::field_h};
  luna::field(p, t, field, is_locked);
  ui_.add_hit_window(this, field, Field);
  const TextStyle style = luna_field_style();
  float text_left = field.x + 4;
  if (!prompt_.prefix.empty()) {
    const auto size = p.text(prompt_.prefix, {text_left, y, 30, field.h}, style, l.ink);
    text_left += size.width + 1;
  }
  float right = field.right() - 2;
  if (prompt_.password) {
    const Rect eye{right - 19, y + 2, 19, 19};
    right = eye.x;
    if (ui_.hot(this, Eye)) p.fill_rect(eye, l.hover_0);
    const float cx = eye.x + 9, cy = eye.y + 9;
    p.fill_rect({cx - 5, cy - 1, 11, 3}, l.bar_arrow);
    p.fill_rect({cx - 2, cy - 3, 5, 7}, l.bar_arrow);
    p.fill_rect({cx - 1, cy - 1, 3, 3}, l.field);
    if (!field_.password) p.fill_rect({cx, cy, 1, 1}, l.bar_arrow);
    ui_.add_hit_window(this, eye, Eye);
  }
  const Rect box{text_left, y, right - 2 - text_left, field.h};
  text_x_ = box.x;
  p.rt()->PushAxisAlignedClip(D2D1::RectF(box.x, box.y, box.right(), box.bottom()), D2D1_ANTIALIAS_MODE_ALIASED);
  const bool focused = ui_.focus == &field_;
  const auto draw_field = [&] {
    return field_.paint(p, {box.x - scroll_x_, box.y, box.w + scroll_x_, box.h}, style, l.ink, widen(prompt_.placeholder), style, l.ink_soft, l.ink,
                        with_alpha(l.select, 0.4f), focused, ui_.caret_on);
  };
  Rect caret = draw_field();
  const float caret_in_text = caret.x + scroll_x_ - box.x;
  float wanted = scroll_x_;
  if (caret_in_text - scroll_x_ > box.w - 2) wanted = caret_in_text - box.w + 2;
  else if (caret_in_text < scroll_x_) wanted = caret_in_text;
  if (wanted != scroll_x_) {
    scroll_x_ = wanted;
    p.fill_rect(box, is_locked ? l.field_off : l.field);
    caret = draw_field();
  }
  p.rt()->PopAxisAlignedClip();
  if (focused) ui_.caret = caret;
  y += luna::field_h + 6;

  p.text(hint_shown_, {left, y, w, hint_h}, hint_style, hint_error ? t.error_text : l.ink_soft);
  y += hint_h + 14;

  const auto button = [&](const std::string& text, float x, int id, luna::ButtonState state) {
    const Rect r{x, y, luna::button_width(p, text), luna::button_h};
    state.hot = ui_.hot(this, id) && !busy_ && !state.off;
    state.pressed = ui_.pressed(this, id) && !busy_;
    luna::button(p, t, r, text, state);
    ui_.add_hit_window(this, r, id);
    return r;
  };
  // Submitting comes last on the right, then the step's other choices, as a dialog has them.
  float x = left + w - luna::button_width(p, tr().ok);
  button(tr().ok, x, Submit, {.off = !(valid_ && !is_locked) || busy_, .primary = true});
  if (!prompt_.extra2.empty()) {
    x -= 6 + luna::button_width(p, prompt_.extra2);
    button(prompt_.extra2, x, Extra2, {});
  }
  if (!prompt_.extra.empty()) {
    x -= 6 + luna::button_width(p, prompt_.extra);
    button(prompt_.extra, x, Extra, {});
  }
  if (prompt_.cancelable) button(tr().prompt_back, left, Back, {});
  y += luna::button_h;
  if (!prompt_.link.empty()) {
    y += 10;
    const Rect row{left, y, w, 16};
    luna::link(p, t, row, prompt_.link, ui_.hot(this, Link));
    ui_.add_hit_window(this, row, Link);
  }
}

TextStyle PromptScreen::luna_field_style() const { return luna::body(ui_.gfx, luna::field_h); }

}  // namespace crpp::ui
