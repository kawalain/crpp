#include "crpp/ui/skin_canvas.hpp"

#include <bit>

#include "crpp/ui/controls.hpp"

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
          .keep_words = look.keep_words,
          .underline = look.underline};
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
  void hit(skin::Box, int, const skin::HitPlace&) override {}
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

void draw_layer(Ui& ui, Painter& p, Surface* owner, const skin::Screen& screen, Rect area, skin::Source& source) {
  SkinCanvas canvas(ui, p, owner);
  canvas.in_window_coordinates();
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

float SkinCanvas::transition(std::uint64_t key, const skin::TransitionSpec& spec) {
  // The same part or screen is told apart by whose it is.
  const std::uint64_t whose = std::bit_cast<std::uint64_t>(whose_ ? whose_ : owner_) * 31 + static_cast<std::uint64_t>(hit_base_);
  bool moving = false;
  const float shown = ui_.transitions.value(key ^ (whose * 0x9E3779B97F4A7C15ull), spec, ui_.now_ms, ui_.animations, moving);
  if (moving) ui_.more_frames = true;
  return shown;
}

void SkinCanvas::path_stretched(std::string_view data, float x, float y, float scale_x, float scale_y, skin::Color color) {
  ID2D1PathGeometry* const shape = p_.gfx().path(data);
  if (!shape) return;
  p_.push(D2D1::Matrix3x2F::Scale(scale_x, scale_y) * D2D1::Matrix3x2F::Translation(x, y));
  p_.fill_geometry(shape, color);
  p_.pop();
}

void SkinCanvas::path_gradient(std::string_view data, float x, float y, float scale, float y0, float y1, std::span<const skin::GradientStop> stops) {
  ID2D1PathGeometry* const shape = p_.gfx().path(data);
  if (!shape) return;
  std::vector<std::pair<float, Color>> list;
  list.reserve(stops.size());
  for (const skin::GradientStop& stop : stops) list.emplace_back(stop.at, stop.color);
  p_.push(scale == 1 ? D2D1::Matrix3x2F::Translation(x, y) : D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y));
  p_.fill_vertical_gradient(shape, y0, y1, list);
  p_.pop();
}

void SkinCanvas::path_shadow(std::string_view data, float x, float y, float scale, float blur, float dy, skin::Color color) {
  ID2D1PathGeometry* const shape = p_.gfx().path(data);
  if (!shape) return;
  p_.push(scale == 1 ? D2D1::Matrix3x2F::Translation(x, y) : D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y));
  p_.shadow_geometry(shape, blur, dy, color);
  p_.pop();
}

bool SkinCanvas::push_opacity(skin::Box box, float opacity) {
  if (opacity >= 1) return false;
  p_.push_opacity(opacity, rect_of(box));
  return true;
}

bool SkinCanvas::push_edge_fade(skin::Box box, float top, float bottom) { return ::crpp::ui::push_edge_fade(ui_, p_, rect_of(box), top, bottom); }

void SkinCanvas::push_clip(skin::Box box) {
  p_.rt()->PushAxisAlignedClip(D2D1::RectF(box.x, box.y, box.x + box.w, box.y + box.h), D2D1_ANTIALIAS_MODE_ALIASED);
  ui_.push_hit_clip(in_window_ ? rect_of(box) : ui_.to_window(rect_of(box)));
}

void SkinCanvas::push_soft_clip(skin::Box box) {
  p_.rt()->PushAxisAlignedClip(D2D1::RectF(box.x, box.y, box.x + box.w, box.y + box.h), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  ui_.push_hit_clip(in_window_ ? rect_of(box) : ui_.to_window(rect_of(box)));
}

void SkinCanvas::pop_clip() {
  ui_.pop_hit_clip();
  p_.rt()->PopAxisAlignedClip();
}

}  // namespace crpp::ui
