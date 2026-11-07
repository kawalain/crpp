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

namespace {

float width_of(const Gfx& gfx, std::string_view text, const skin::TextLook& look, float width) {
  DWRITE_TEXT_METRICS m{};
  if (const auto layout = gfx.layout(widen(text), text_style_of(look), width)) layout->GetMetrics(&m);
  return m.widthIncludingTrailingWhitespace;
}

float height_of(const Gfx& gfx, std::string_view text, const skin::TextLook& look, float width) {
  DWRITE_TEXT_METRICS m{};
  if (const auto layout = gfx.layout(widen(text), text_style_of(look), width, 100000)) layout->GetMetrics(&m);
  return m.height;
}

class MeasureCanvas final : public skin::Canvas {
 public:
  explicit MeasureCanvas(const Gfx& gfx) : gfx_(gfx) {}

  skin::Box snap(skin::Box box) override { return box; }
  float snap(float value) override { return value; }
  float line() override { return 1; }
  float font_size() override { return gfx_.classic_size(); }
  float text_width(std::string_view text, const skin::TextLook& look, float width) override { return width_of(gfx_, text, look, width); }
  float text_height(std::string_view text, const skin::TextLook& look, float width) override { return height_of(gfx_, text, look, width); }
  void fill(skin::Box, skin::Color) override {}
  void fill_down(skin::Box, float, float, std::span<const skin::GradientStop>) override {}
  void fill_round(skin::Box, float, skin::Color) override {}
  void fill_round_down(skin::Box, float, float, float, std::span<const skin::GradientStop>) override {}
  void frame(skin::Box, skin::Color) override {}
  void stroke_round(skin::Box, float, skin::Color, float) override {}
  void line(float, float, float, float, skin::Color, float) override {}
  void path(std::string_view, float, float, float, skin::Color, float, bool, bool) override {}
  void text(std::string_view, skin::Box, const skin::TextLook&, skin::Color) override {}
  void push_clip(skin::Box) override {}
  void pop_clip() override {}
  void hit(skin::Box, int) override {}
  bool hot(int) override { return false; }
  bool pressed(int) override { return false; }

 private:
  const Gfx& gfx_;
};

}  // namespace

void draw_screen(Ui& ui, Painter& p, Surface* owner, const skin::Screen& screen, Rect area, skin::Source& source) {
  SkinCanvas canvas(ui, p, owner);
  skin::draw(*ui.pack, screen, ui.palette, {area.x, area.y, area.w, area.h}, canvas, source);
}

void measure_screen(const Ui& ui, const skin::Screen& screen, Rect area, skin::Source& source) {
  MeasureCanvas canvas(ui.gfx);
  skin::draw(*ui.pack, screen, ui.palette, {area.x, area.y, area.w, area.h}, canvas, source);
}

skin::Box SkinCanvas::snap(skin::Box box) {
  const Rect r = p_.snap(rect_of(box));
  return {r.x, r.y, r.w, r.h};
}

float SkinCanvas::text_width(std::string_view text, const skin::TextLook& look, float width) { return width_of(p_.gfx(), text, look, width); }

float SkinCanvas::snap(float value) { return p_.snap({value, 0, 1, 1}).x; }

float SkinCanvas::text_height(std::string_view text, const skin::TextLook& look, float width) { return height_of(p_.gfx(), text, look, width); }

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
