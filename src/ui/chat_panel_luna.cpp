// The chat in the Luna Skin: "name says:" above an indented message, in a white list, with the
// message being written under it. Shares its log, limiter and callbacks with the Cloud layout.
#include <algorithm>
#include <cmath>

#include "crpp/ui/chat_panel.hpp"
#include "crpp/ui/luna.hpp"

namespace crpp::ui {

namespace {
constexpr float indent = 14, list_pad = 7, row_gap = 7;
}  // namespace

// The chat's size setting is a font size here, so the letters stay crisp.
TextStyle ChatPanel::luna_body() const {
  const float size = std::round(ui_.gfx.classic_size() * zoom_);
  return {.size = size, .line_height = size + 4, .wrap = true, .exact = true};
}

TextStyle ChatPanel::luna_input() const {
  const float size = ui_.gfx.classic_size();
  return {.size = size, .line_height = size + 4, .wrap = true, .exact = true};
}

void ChatPanel::paint_luna(Painter& p, Rect panel) {
  const Theme& t = ui_.theme;
  const LunaColors& l = t.luna;
  const TextStyle input_style = luna_input();
  const float send_w = luna::button_w;
  const float field_w = panel.w - send_w - 6;
  input_text_w_ = field_w - 8;
  const float input_h = std::clamp(std::ceil(input_.measure_height(ui_.gfx, input_style, field_w - 8)) + 8, 52.0f, 100.0f);
  const Rect list{panel.x, panel.y, panel.w, panel.h - input_h - 6};
  area_ = list;
  luna::sunken(p, t, list);
  ui_.add_hit_window(this, list, Area);

  // --- the messages ---
  const Rect inner{list.x + 1, list.y + 1, list.w - 2, list.h - 2};
  messages_ = {.indent = indent, .pad = list_pad, .gap = row_gap, .bar_w = luna::scrollbar_w, .body = luna_body(), .ink = l.ink, .soft = l.ink_soft};
  paint_messages(p, inner);

  const Rect bar{inner.right() - luna::scrollbar_w, inner.y, luna::scrollbar_w, inner.h};
  const auto parts = luna::scrollbar_parts(bar, scroll_.view_h, scroll_.content_h, scroll_.offset);
  bar_track_ = parts.track;
  luna::scrollbar(p, t, parts, ui_.hot(this, BarThumb), ui_.hot(this, BarUp), ui_.hot(this, BarDown));
  ui_.add_hit_window(this, bar, BarTrack);
  ui_.add_hit_window(this, parts.up, BarUp);
  ui_.add_hit_window(this, parts.down, BarDown);
  ui_.add_hit_window(this, parts.thumb, BarThumb);

  // --- what is being written ---
  const Rect field{panel.x, panel.bottom() - input_h, field_w, input_h};
  luna::field(p, t, field);
  const bool focused = ui_.focus == &input_;
  const Rect box{field.x + 4, field.y + 4, field.w - 8, field.h - 8};
  p.rt()->PushAxisAlignedClip(D2D1::RectF(field.x + 1, field.y + 1, field.right() - 1, field.bottom() - 1), D2D1_ANTIALIAS_MODE_ALIASED);
  const Rect caret = input_.paint(p, box, input_style, l.ink, widen(tr().chat_placeholder), input_style, l.ink_soft, l.ink, with_alpha(l.select, 0.4f), focused, ui_.caret_on);
  p.rt()->PopAxisAlignedClip();
  if (focused) ui_.caret = caret;
  ui_.add_hit_window(this, field, Input);

  const Rect send{panel.right() - send_w, field.y, send_w, input_h};
  const bool off = input_.text().empty() || limiter_.cooling();
  luna::button(p, t, send, tr().chat_send, {.hot = !off && ui_.hot(this, Send), .pressed = !off && ui_.pressed(this, Send), .off = off});
  ui_.add_hit_window(this, send, Send);

  // Too fast: said in a small box, over the button.
  if (limiter_.calm()) {
    const std::string text = limiter_.calm_text();
    DWRITE_TEXT_METRICS m{};
    const TextStyle style = luna::body(p, 16, Align::Center);
    if (const auto layout = ui_.gfx.layout(widen(text), style, 400)) layout->GetMetrics(&m);
    const float w = std::ceil(m.widthIncludingTrailingWhitespace) + 10;
    const Rect tip_box{std::max(panel.x, send.right() - w), send.y - 22, w, 18};
    const Color ink = ui_.skin().bubble(p, t, tip_box, 0, {}, {}, 1);
    p.text(text, tip_box, style, ink);
  }

  link_tip_.paint(ui_, p, show_link_tip_ ? &link_tip_spec_ : nullptr);
}

}  // namespace crpp::ui
