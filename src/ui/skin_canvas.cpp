#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

TextStyle text_style_of(const skin::TextLook& look) {
  return {.size = look.size,
          .weight = look.weight,
          .line_height = look.line_height,
          .align = static_cast<Align>(look.align),
          .wrap = look.wrap,
          .ellipsis = look.ellipsis,
          .exact = look.exact,
          .keep_words = look.keep_words};
}

skin::Box SkinCanvas::snap(skin::Box box) {
  const Rect r = p_.snap(rect_of(box));
  return {r.x, r.y, r.w, r.h};
}

float SkinCanvas::text_width(std::string_view text, const skin::TextLook& look) {
  DWRITE_TEXT_METRICS m{};
  if (const auto layout = p_.gfx().layout(widen(text), text_style_of(look), 1000)) layout->GetMetrics(&m);
  return m.widthIncludingTrailingWhitespace;
}

void SkinCanvas::push_clip(skin::Box box) {
  p_.rt()->PushAxisAlignedClip(D2D1::RectF(box.x, box.y, box.x + box.w, box.y + box.h), D2D1_ANTIALIAS_MODE_ALIASED);
  ui_.push_hit_clip(ui_.to_window(rect_of(box)));
}

void SkinCanvas::pop_clip() {
  ui_.pop_hit_clip();
  p_.rt()->PopAxisAlignedClip();
}

}  // namespace crpp::ui
