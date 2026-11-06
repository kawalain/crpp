#include "crpp/ui/gfx.hpp"

#include <dwrite_1.h>
#include <windows.h>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace crpp::ui {

const char* const cloud_path_data =
    "M388.527 317.44H101.915C54.679 317.44 16.386 279.073 16.386 231.744c0-42.356 30.678-77.512 70.979-84.436-.747-4.387-1.158-8.888-1.158-13.489 0-44 35.6-79.669 79.513-79.669 14.343 0 27.786 3.824 39.404 10.482C226.646 30.651 264.524 8.095 307.663 8.095c63.092 0 114.932 48.241 120.801 109.916 37.233 15.702 63.375 52.597 63.375 95.613 0 57.208-46.238 103.593-103.312 103.69Z";

std::wstring widen(std::string_view s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string narrow(std::wstring_view w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

namespace {

constexpr const wchar_t* ui_font = L"Malgun Gothic";

D2D1_COLOR_F to_d2d(Color c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }

// --- SVG path data ---------------------------------------------------------

class PathReader {
 public:
  explicit PathReader(std::string_view s) : s_(s) {}

  bool more() {
    skip_separators();
    return pos_ < s_.size();
  }
  bool at_command() {
    skip_separators();
    return pos_ < s_.size() && std::isalpha(static_cast<unsigned char>(s_[pos_]));
  }
  char command() { return s_[pos_++]; }
  bool has_number() {
    skip_separators();
    if (pos_ >= s_.size()) return false;
    const char c = s_[pos_];
    return std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.';
  }
  float number() {
    skip_separators();
    const char* begin = s_.data() + pos_;
    char* end = nullptr;
    const float v = std::strtof(begin, &end);
    pos_ += static_cast<std::size_t>(end - begin);
    return v;
  }
  // Arc flags are single characters and may be written without separators.
  bool flag() {
    skip_separators();
    const bool v = pos_ < s_.size() && s_[pos_] == '1';
    ++pos_;
    return v;
  }

 private:
  void skip_separators() {
    while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == ',' || s_[pos_] == '\t' || s_[pos_] == '\n')) ++pos_;
  }
  std::string_view s_;
  std::size_t pos_ = 0;
};

}  // namespace

bool Gfx::init() {
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.put()))) return false;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(dwrite_.put())))) {
    return false;
  }
  // Font metrics, to place text the way a CSS line box does.
  ComPtr<IDWriteFontCollection> fonts;
  if (SUCCEEDED(dwrite_->GetSystemFontCollection(fonts.put(), FALSE))) {
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(fonts->FindFamilyName(ui_font, &index, &exists)) && exists) {
      ComPtr<IDWriteFontFamily> family;
      ComPtr<IDWriteFont> font;
      if (SUCCEEDED(fonts->GetFontFamily(index, family.put())) &&
          SUCCEEDED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                 DWRITE_FONT_STYLE_NORMAL, font.put()))) {
        DWRITE_FONT_METRICS m{};
        font->GetMetrics(&m);
        if (m.designUnitsPerEm > 0) {
          ascent_ = static_cast<float>(m.ascent) / m.designUnitsPerEm;
          descent_ = static_cast<float>(m.descent) / m.designUnitsPerEm;
        }
      }
    }
  }
  return true;
}

ComPtr<ID2D1PathGeometry> Gfx::parse_path(std::string_view d) const {
  ComPtr<ID2D1PathGeometry> geometry;
  if (FAILED(d2d_->CreatePathGeometry(geometry.put()))) return {};
  ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(geometry->Open(sink.put()))) return {};

  PathReader in(d);
  float cx = 0, cy = 0, sx = 0, sy = 0;  // current and subpath start
  float lcx = 0, lcy = 0;                // last cubic control point
  bool open = false;
  char cmd = 0;
  char prev = 0;
  const auto begin = [&](float x, float y) {
    if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->BeginFigure(D2D1::Point2F(x, y), D2D1_FIGURE_BEGIN_FILLED);
    open = true;
    cx = sx = x;
    cy = sy = y;
  };
  while (in.more()) {
    if (in.at_command()) {
      cmd = in.command();
    } else if (cmd == 'M') {
      cmd = 'L';  // extra coordinate pairs after a moveto are linetos
    } else if (cmd == 'm') {
      cmd = 'l';
    }
    const bool rel = std::islower(static_cast<unsigned char>(cmd)) != 0;
    const float ox = rel ? cx : 0, oy = rel ? cy : 0;
    switch (std::toupper(static_cast<unsigned char>(cmd))) {
      case 'M': {
        const float x = in.number() + ox, y = in.number() + oy;
        begin(x, y);
        break;
      }
      case 'L': {
        const float x = in.number() + ox, y = in.number() + oy;
        sink->AddLine(D2D1::Point2F(x, y));
        cx = x;
        cy = y;
        break;
      }
      case 'H': {
        const float x = in.number() + ox;
        sink->AddLine(D2D1::Point2F(x, cy));
        cx = x;
        break;
      }
      case 'V': {
        const float y = in.number() + oy;
        sink->AddLine(D2D1::Point2F(cx, y));
        cy = y;
        break;
      }
      case 'C': {
        const float x1 = in.number() + ox, y1 = in.number() + oy;
        const float x2 = in.number() + ox, y2 = in.number() + oy;
        const float x = in.number() + ox, y = in.number() + oy;
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), D2D1::Point2F(x, y)));
        lcx = x2;
        lcy = y2;
        cx = x;
        cy = y;
        break;
      }
      case 'S': {
        const bool after_cubic = std::toupper(static_cast<unsigned char>(prev)) == 'C' ||
                                 std::toupper(static_cast<unsigned char>(prev)) == 'S';
        const float x1 = after_cubic ? 2 * cx - lcx : cx, y1 = after_cubic ? 2 * cy - lcy : cy;
        const float x2 = in.number() + ox, y2 = in.number() + oy;
        const float x = in.number() + ox, y = in.number() + oy;
        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), D2D1::Point2F(x, y)));
        lcx = x2;
        lcy = y2;
        cx = x;
        cy = y;
        break;
      }
      case 'A': {
        const float rx = in.number(), ry = in.number(), rot = in.number();
        const bool large = in.flag(), sweep = in.flag();
        const float x = in.number() + ox, y = in.number() + oy;
        sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x, y), D2D1::SizeF(rx, ry), rot,
                                      sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                      large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
        cx = x;
        cy = y;
        break;
      }
      case 'Z':
        if (open) sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        open = false;
        cx = sx;
        cy = sy;
        break;
      default:
        return {};  // unsupported command
    }
    prev = cmd;
  }
  if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
  if (FAILED(sink->Close())) return {};
  return geometry;
}

ComPtr<IDWriteTextLayout> Gfx::layout(std::wstring_view text, const TextStyle& style, float max_w,
                                      float max_h) const {
  ComPtr<IDWriteTextFormat> format;
  if (FAILED(dwrite_->CreateTextFormat(ui_font, nullptr, static_cast<DWRITE_FONT_WEIGHT>(style.weight),
                                       DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, style.size,
                                       L"ko-kr", format.put()))) {
    return {};
  }
  format->SetWordWrapping(style.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
  format->SetTextAlignment(style.align == Align::Center  ? DWRITE_TEXT_ALIGNMENT_CENTER
                           : style.align == Align::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                         : DWRITE_TEXT_ALIGNMENT_LEADING);
  if (style.line_height > 0) {
    // Place the baseline where a CSS line box of this height would.
    const float content = (ascent_ + descent_) * style.size;
    const float baseline = (style.line_height - content) / 2 + ascent_ * style.size;
    format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, style.line_height, baseline);
  }
  ComPtr<IDWriteTextLayout> layout;
  if (FAILED(dwrite_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format.get(), max_w, max_h,
                                       layout.put()))) {
    return {};
  }
  if (style.ellipsis) {
    ComPtr<IDWriteInlineObject> sign;
    if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(format.get(), sign.put()))) {
      DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
      layout->SetTrimming(&trimming, sign.get());
    }
  }
  if (style.letter_spacing != 0) {
    ComPtr<IDWriteTextLayout1> l1;
    if (SUCCEEDED(layout->QueryInterface(__uuidof(IDWriteTextLayout1), reinterpret_cast<void**>(l1.put())))) {
      l1->SetCharacterSpacing(0, style.letter_spacing, 0, DWRITE_TEXT_RANGE{0, static_cast<UINT32>(text.size())});
    }
  }
  return layout;
}

// --- Painter ---------------------------------------------------------------

Painter::Painter(const Gfx& gfx, ID2D1RenderTarget* rt) : gfx_(gfx), rt_(rt) {
  rt_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), brush_.put());
  rt_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
}

void Painter::push(const D2D1_MATRIX_3X2_F& m) { rt_->SetTransform(m * base_); }

void Painter::clear(Color c) { rt_->Clear(to_d2d(c)); }

void Painter::fill_rect(Rect r, Color c) {
  brush_->SetColor(to_d2d(c));
  rt_->FillRectangle(D2D1::RectF(r.x, r.y, r.right(), r.bottom()), brush_.get());
}

void Painter::fill_round(Rect r, float radius, Color c) {
  brush_->SetColor(to_d2d(c));
  rt_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.x, r.y, r.right(), r.bottom()), radius, radius),
                            brush_.get());
}

void Painter::fill_ellipse(float cx, float cy, float rx, float ry, Color c) {
  brush_->SetColor(to_d2d(c));
  rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), brush_.get());
}

void Painter::fill_geometry(ID2D1Geometry* g, Color c) {
  brush_->SetColor(to_d2d(c));
  rt_->FillGeometry(g, brush_.get());
}

namespace {
ComPtr<ID2D1StrokeStyle> round_stroke(ID2D1Factory* f) {
  ComPtr<ID2D1StrokeStyle> s;
  f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                   D2D1_LINE_JOIN_ROUND),
                       nullptr, 0, s.put());
  return s;
}
}  // namespace

void Painter::stroke_geometry(ID2D1Geometry* g, Color c, float width, bool round_caps) {
  brush_->SetColor(to_d2d(c));
  ComPtr<ID2D1StrokeStyle> style = round_caps ? round_stroke(gfx_.d2d()) : ComPtr<ID2D1StrokeStyle>{};
  rt_->DrawGeometry(g, brush_.get(), width, style.get());
}

void Painter::stroke_line(float x1, float y1, float x2, float y2, Color c, float width) {
  brush_->SetColor(to_d2d(c));
  ComPtr<ID2D1StrokeStyle> style = round_stroke(gfx_.d2d());
  rt_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush_.get(), width, style.get());
}

void Painter::stroke_ellipse(float cx, float cy, float rx, float ry, Color c, float width) {
  brush_->SetColor(to_d2d(c));
  rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), brush_.get(), width);
}

void Painter::shadow_round(Rect r, float radius, float blur, float dy, Color c) {
  // Layered, progressively larger translucent shapes approximate a Gaussian.
  constexpr int steps = 12;
  for (int i = steps; i >= 1; --i) {
    const float grow = blur * static_cast<float>(i) / steps;
    const float a = c.a / steps * 1.6f;
    fill_round({r.x - grow, r.y - grow + dy, r.w + 2 * grow, r.h + 2 * grow}, radius + grow, with_alpha(c, a));
  }
}

void Painter::draw_layout(IDWriteTextLayout* layout, float x, float y, Color c) {
  brush_->SetColor(to_d2d(c));
  rt_->DrawTextLayout(D2D1::Point2F(x, y), layout, brush_.get());
}

D2D1_SIZE_F Painter::text(std::wstring_view s, Rect r, const TextStyle& style, Color c) {
  const auto layout = gfx_.layout(s, style, r.w, style.wrap ? 10000 : r.h);
  if (!layout) return {0, 0};
  brush_->SetColor(to_d2d(c));
  rt_->DrawTextLayout(D2D1::Point2F(r.x, r.y), layout.get(), brush_.get());
  DWRITE_TEXT_METRICS m{};
  layout->GetMetrics(&m);
  return D2D1::SizeF(m.widthIncludingTrailingWhitespace, m.height);
}

void Painter::cloud(ID2D1PathGeometry* shape, float x, float y, float scale, float shadow_alpha, float sigma,
                    const Theme& theme) {
  push(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y));
  if (theme.cloud_shadow_scale > 0) {
    // Canvas shadowBlur = sigma * 3 with a 6 px vertical offset: layer translucent copies.
    const float blur = sigma * 3;
    constexpr int steps = 10;
    for (int i = steps; i >= 1; --i) {
      const float grow = blur * static_cast<float>(i) / steps;
      const Color shadow = with_alpha(theme.cloud_shadow, shadow_alpha * 1.6f / steps);
      rt_->SetTransform(D2D1::Matrix3x2F::Translation(0, 6 / scale) *
                        D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y) * base_);
      brush_->SetColor(to_d2d(shadow));
      rt_->FillGeometry(shape, brush_.get());
      ComPtr<ID2D1StrokeStyle> round = round_stroke(gfx_.d2d());
      rt_->DrawGeometry(shape, brush_.get(), 2 * grow / scale, round.get());
    }
    push(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y));
  }
  fill_geometry(shape, theme.cloud_base);
  // Vertical gradient top -> bottom as in drawCloud.
  D2D1_GRADIENT_STOP stops[2] = {{0.0f, to_d2d(theme.cloud_top)}, {1.0f, to_d2d(theme.cloud_bottom)}};
  ComPtr<ID2D1GradientStopCollection> collection;
  ComPtr<ID2D1LinearGradientBrush> gradient;
  if (SUCCEEDED(rt_->CreateGradientStopCollection(stops, 2, collection.put())) &&
      SUCCEEDED(rt_->CreateLinearGradientBrush(
          D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 8), D2D1::Point2F(0, 318)), collection.get(),
          gradient.put()))) {
    rt_->FillGeometry(shape, gradient.get());
  }
  rt_->SetTransform(base_);
}

}  // namespace crpp::ui
