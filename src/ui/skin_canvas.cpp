#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

TextStyle text_style_of(const skin::TextLook& look) {
  return {.size = look.size,
          .weight = look.weight,
          .line_height = look.line_height,
          .align = static_cast<Align>(look.align),
          .wrap = look.wrap,
          .ellipsis = look.ellipsis,
          .letter_spacing = look.letter_spacing,
          .exact = look.exact,
          .role = static_cast<FontRole>(look.role),
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

float SkinCanvas::snap(float value) { return p_.snap({value, 0, 1, 1}).x; }

float SkinCanvas::text_height(std::string_view text, const skin::TextLook& look, float width) {
  DWRITE_TEXT_METRICS m{};
  if (const auto layout = p_.gfx().layout(widen(text), text_style_of(look), width, 100000)) layout->GetMetrics(&m);
  return m.height;
}

void SkinCanvas::path(std::string_view data, float x, float y, float scale, skin::Color color, float width, bool round_caps, bool soft) {
  ID2D1PathGeometry* const shape = p_.gfx().path(data);
  if (!shape) return;
  const D2D1_ANTIALIAS_MODE mode = p_.rt()->GetAntialiasMode();
  if (soft) p_.rt()->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  p_.push(scale == 1 ? D2D1::Matrix3x2F::Translation(x, y) : D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y));
  if (width > 0) p_.stroke_geometry(shape, color, width, round_caps);
  else p_.fill_geometry(shape, color);
  p_.pop();
  p_.rt()->SetAntialiasMode(mode);
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
