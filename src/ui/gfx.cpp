#include "crpp/ui/gfx.hpp"

#include <dwrite_1.h>
#include <dwrite_3.h>
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <numbers>
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

constexpr const wchar_t* ui_font = L"Malgun Gothic";            // fallback
constexpr const wchar_t* custom_font = L"Pretendard GOV Variable";  // family name inside the fetched font
constexpr const wchar_t* emoji_font = L"Segoe UI Emoji";

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

ID2D1Factory* Gfx::d2d() const {
  if (!d2d_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.put()))) d2d_.reset();
  return d2d_.get();
}

void Gfx::release_factory() {
  paths_.clear();
  d2d_.reset();
}

bool Gfx::init(const std::filesystem::path& font_file) {
  if (!d2d()) return false;
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(dwrite_.put())))) {
    return false;
  }
  load_font(font_file);
  return true;
}

void Gfx::set_language(std::wstring tag) {
  language_ = std::move(tag);
  if (classic_) choose_classic_fonts();
}

void Gfx::set_classic(bool on) {
  if (on == classic_) return;
  classic_ = on;
  if (on) {
    fonts_.reset();  // nothing will use it
    choose_classic_fonts();
  } else {
    classic_fallback_.reset();
    load_font(font_file_);
  }
}

Gfx::Metrics Gfx::metrics_of(const std::wstring& family) const {
  Metrics metrics;
  ComPtr<IDWriteFontCollection> system_fonts;
  UINT32 index = 0;
  BOOL exists = FALSE;
  ComPtr<IDWriteFontFamily> found;
  ComPtr<IDWriteFont> font;
  if (SUCCEEDED(dwrite_->GetSystemFontCollection(system_fonts.put(), FALSE)) &&
      SUCCEEDED(system_fonts->FindFamilyName(family.c_str(), &index, &exists)) && exists &&
      SUCCEEDED(system_fonts->GetFontFamily(index, found.put())) &&
      SUCCEEDED(found->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, font.put()))) {
    DWRITE_FONT_METRICS m{};
    font->GetMetrics(&m);
    if (m.designUnitsPerEm > 0) {
      metrics.ascent = static_cast<float>(m.ascent) / m.designUnitsPerEm;
      metrics.descent = static_cast<float>(m.descent) / m.designUnitsPerEm;
    }
  }
  return metrics;
}

void Gfx::choose_classic_fonts() {
  ComPtr<IDWriteFontCollection> system_fonts;
  const auto installed = [&](const wchar_t* family) {
    UINT32 index = 0;
    BOOL exists = FALSE;
    return system_fonts && SUCCEEDED(system_fonts->FindFamilyName(family, &index, &exists)) && exists;
  };
  dwrite_->GetSystemFontCollection(system_fonts.put(), FALSE);
  // Gulim comes with an optional package of Windows; Malgun Gothic is always there.
  const wchar_t* const hangul = installed(L"Gulim") ? L"Gulim" : L"Malgun Gothic";
  const bool korean = language_.starts_with(L"ko");
  classic_body_ = korean ? hangul : L"Tahoma";
  classic_size_ = korean ? 12.0f : 11.0f;
  classic_caption_ = L"Trebuchet MS";
  classic_body_metrics_ = metrics_of(classic_body_);
  classic_caption_metrics_ = metrics_of(classic_caption_);

  // Whatever the font of the text, Hangul takes this one.
  classic_fallback_.reset();
  ComPtr<IDWriteFactory2> factory2;
  ComPtr<IDWriteFontFallbackBuilder> builder;
  ComPtr<IDWriteFontFallback> system_fallback;
  if (SUCCEEDED(dwrite_->QueryInterface(__uuidof(IDWriteFactory2), reinterpret_cast<void**>(factory2.put()))) &&
      SUCCEEDED(factory2->CreateFontFallbackBuilder(builder.put())) && SUCCEEDED(factory2->GetSystemFontFallback(system_fallback.put()))) {
    const DWRITE_UNICODE_RANGE ranges[] = {{0x1100, 0x11FF}, {0x3130, 0x318F}, {0xA960, 0xA97F}, {0xAC00, 0xD7FF}};
    const WCHAR* families[] = {hangul};
    builder->AddMapping(ranges, 4, families, 1);
    builder->AddMappings(system_fallback.get());
    builder->CreateFontFallback(classic_fallback_.put());
  }
}

void Gfx::load_font(const std::filesystem::path& font_file) {
  font_file_ = font_file;
  fonts_.reset();
  family_ = ui_font;
  if (!font_file.empty()) {
    // A private font collection built from the file (DirectWrite 3; Windows 10 1703+).
    ComPtr<IDWriteFactory5> factory5;
    ComPtr<IDWriteFontSetBuilder1> builder;
    ComPtr<IDWriteFontFile> file;
    ComPtr<IDWriteFontSet> set;
    ComPtr<IDWriteFontCollection1> collection;
    if (SUCCEEDED(dwrite_->QueryInterface(__uuidof(IDWriteFactory5), reinterpret_cast<void**>(factory5.put()))) &&
        SUCCEEDED(factory5->CreateFontSetBuilder(builder.put())) &&
        SUCCEEDED(dwrite_->CreateFontFileReference(font_file.c_str(), nullptr, file.put())) &&
        SUCCEEDED(builder->AddFontFile(file.get())) && SUCCEEDED(builder->CreateFontSet(set.put())) &&
        SUCCEEDED(factory5->CreateFontCollectionFromFontSet(set.get(), collection.put()))) {
      UINT32 index = 0;
      BOOL exists = FALSE;
      if (SUCCEEDED(collection->FindFamilyName(custom_font, &index, &exists)) && exists) {
        fonts_ = collection.upcast<IDWriteFontCollection>();
        family_ = custom_font;
      }
    }
  }
  // Font metrics, to place text the way a CSS line box does.
  ComPtr<IDWriteFontCollection> system_fonts;
  IDWriteFontCollection* fonts = fonts_.get();
  if (!fonts && SUCCEEDED(dwrite_->GetSystemFontCollection(system_fonts.put(), FALSE))) fonts = system_fonts.get();
  if (fonts) {
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(fonts->FindFamilyName(family_.c_str(), &index, &exists)) && exists) {
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
}

ID2D1PathGeometry* Gfx::path(std::string_view d) const {
  if (const auto found = paths_.find(d); found != paths_.end()) return found->second.get();
  auto geometry = parse_path(d);
  if (!geometry) return nullptr;
  return paths_.emplace(std::string(d), std::move(geometry)).first->second.get();
}

ComPtr<ID2D1PathGeometry> Gfx::parse_path(std::string_view d) const {
  ComPtr<ID2D1PathGeometry> geometry;
  ID2D1Factory* const factory = d2d();
  if (!factory || FAILED(factory->CreatePathGeometry(geometry.put()))) return {};
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

ComPtr<IDWriteTextLayout> Gfx::emoji_layout(std::wstring_view text, float size) const {
  ComPtr<IDWriteTextFormat> format;
  ComPtr<IDWriteTextLayout> layout;
  if (FAILED(dwrite_->CreateTextFormat(emoji_font, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL, size, L"", format.put())) ||
      FAILED(dwrite_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format.get(), 10000, 10000, layout.put()))) {
    return {};
  }
  return layout;
}

std::wstring Gfx::emoji_glyph(std::string_view id) const {
  ComPtr<IDWriteFontCollection> fonts;
  ComPtr<IDWriteFontFamily> family;
  ComPtr<IDWriteFont> font;
  UINT32 index = 0;
  BOOL exists = FALSE;
  if (FAILED(dwrite_->GetSystemFontCollection(fonts.put(), FALSE)) ||
      FAILED(fonts->FindFamilyName(emoji_font, &index, &exists)) || !exists ||
      FAILED(fonts->GetFontFamily(index, family.put())) ||
      FAILED(family->GetFirstMatchingFont(DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                          font.put()))) {
    return {};
  }
  std::wstring text;
  while (!id.empty()) {
    const auto dash = id.find('-');
    const std::string_view part = id.substr(0, dash);
    id = dash == std::string_view::npos ? std::string_view{} : id.substr(dash + 1);
    UINT32 code = 0;
    const auto [end, error] = std::from_chars(part.data(), part.data() + part.size(), code, 16);
    if (error != std::errc{} || end != part.data() + part.size() || code < 0x100 || code > 0x10FFFF) return {};
    if (code != 0x200D && code != 0xFE0F) {
      BOOL has = FALSE;
      if (FAILED(font->HasCharacter(code, &has)) || !has) return {};
    }
    if (code >= 0x10000) {
      text += static_cast<wchar_t>(0xD800 + ((code - 0x10000) >> 10));
      text += static_cast<wchar_t>(0xDC00 + ((code - 0x10000) & 0x3FF));
    } else {
      text += static_cast<wchar_t>(code);
    }
  }
  // A sequence the font does not join comes out as several glyphs, wider than one.
  constexpr float probe = 100;
  const auto layout = emoji_layout(text, probe);
  DWRITE_TEXT_METRICS metrics{};
  if (!layout || FAILED(layout->GetMetrics(&metrics)) || metrics.width > probe * 1.5f) return {};
  return text;
}

void Gfx::apply_style(IDWriteTextFormat* format, const TextStyle& style, Metrics metrics) const {
  format->SetWordWrapping(style.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
  format->SetTextAlignment(style.align == Align::Center  ? DWRITE_TEXT_ALIGNMENT_CENTER
                           : style.align == Align::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                         : DWRITE_TEXT_ALIGNMENT_LEADING);
  if (style.line_height > 0) {
    // Place the baseline where a CSS line box of this height would.
    const float content = (metrics.ascent + metrics.descent) * style.size;
    const float baseline = (style.line_height - content) / 2 + metrics.ascent * style.size;
    format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, style.line_height, baseline);
  }
}

void Gfx::finish_layout(IDWriteTextLayout* layout, IDWriteTextFormat* format, std::wstring_view text, const TextStyle& style) const {
  if (style.ellipsis) {
    ComPtr<IDWriteInlineObject> sign;
    if (SUCCEEDED(dwrite_->CreateEllipsisTrimmingSign(format, sign.put()))) {
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
}

ComPtr<IDWriteTextLayout> Gfx::layout(std::wstring_view text, const TextStyle& style, float max_w,
                                      float max_h) const {
  if (classic_) return classic_layout(text, style, max_w, max_h);
  ComPtr<IDWriteTextFormat> format;
  if (FAILED(dwrite_->CreateTextFormat(family_.c_str(), fonts_.get(), static_cast<DWRITE_FONT_WEIGHT>(style.weight),
                                       DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, style.size,
                                       language_.c_str(), format.put()))) {
    return {};
  }
  apply_style(format.get(), style, {ascent_, descent_});
  ComPtr<IDWriteTextLayout> layout;
  if (FAILED(dwrite_->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format.get(), max_w, max_h,
                                       layout.put()))) {
    return {};
  }
  finish_layout(layout.get(), format.get(), text, style);
  return layout;
}

ComPtr<IDWriteTextLayout> Gfx::classic_layout(std::wstring_view text, TextStyle style, float max_w, float max_h) const {
  if (!style.exact) {
    // What a shared surface asks for as a title stays a title; all else is plain body text.
    const bool title = style.size >= 21;
    style.size = title ? classic_size_ + 2 : classic_size_;
    style.weight = title ? 700 : 400;
    style.letter_spacing = 0;
    if (style.wrap) style.line_height = classic_size_ + 4;
  }
  std::wstring kept;
  if (style.keep_words) {
    // DirectWrite has no word-break: keep-all, so a word joiner holds each pair of syllables.
    const auto syllable = [](wchar_t c) { return c >= 0xAC00 && c <= 0xD7A3; };
    kept.reserve(text.size() + text.size() / 2);
    for (std::size_t i = 0; i < text.size(); ++i) {
      kept.push_back(text[i]);
      if (i + 1 < text.size() && syllable(text[i]) && syllable(text[i + 1])) kept.push_back(L'\x2060');
    }
    text = kept;
  }
  const bool caption = style.role == FontRole::Caption;
  const std::wstring& family = caption ? classic_caption_ : classic_body_;
  ComPtr<IDWriteTextFormat> format;
  if (FAILED(dwrite_->CreateTextFormat(family.c_str(), nullptr, static_cast<DWRITE_FONT_WEIGHT>(style.weight), DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL, style.size, language_.c_str(), format.put()))) {
    return {};
  }
  apply_style(format.get(), style, caption ? classic_caption_metrics_ : classic_body_metrics_);
  if (classic_fallback_) {
    ComPtr<IDWriteTextFormat1> format1;
    if (SUCCEEDED(format->QueryInterface(__uuidof(IDWriteTextFormat1), reinterpret_cast<void**>(format1.put())))) {
      format1->SetFontFallback(classic_fallback_.get());
    }
  }
  ComPtr<IDWriteTextLayout> layout;
  if (FAILED(dwrite_->CreateGdiCompatibleTextLayout(text.data(), static_cast<UINT32>(text.size()), format.get(), max_w, max_h, pixels_per_dip_,
                                                    nullptr, FALSE, layout.put()))) {
    return {};
  }
  finish_layout(layout.get(), format.get(), text, style);
  return layout;
}

ID2D1LinearGradientBrush* Gfx::gradient(ID2D1RenderTarget* rt, std::span<const GradientStop> stops) const {
  if (gradients_target_ != rt) {
    gradients_.clear();
    gradients_target_ = rt;
  }
  auto& brush = gradients_[stops.data()];
  if (brush) return brush.get();
  std::vector<D2D1_GRADIENT_STOP> list;
  for (const GradientStop& stop : stops) list.push_back({stop.at, D2D1::ColorF(stop.color.r, stop.color.g, stop.color.b, stop.color.a)});
  ComPtr<ID2D1GradientStopCollection> collection;
  if (FAILED(rt->CreateGradientStopCollection(list.data(), static_cast<UINT32>(list.size()), collection.put())) ||
      FAILED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 1)), collection.get(),
                                           brush.put()))) {
    gradients_.erase(stops.data());
    return nullptr;
  }
  return brush.get();
}

void Gfx::forget_render_target() const {
  gradients_.clear();
  gradients_target_ = nullptr;
}

// --- Painter ---------------------------------------------------------------

Painter::Painter(const Gfx& gfx, ID2D1RenderTarget* rt) : gfx_(gfx), rt_(rt) {
  rt_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), brush_.put());
  const bool classic = gfx.classic();
  rt_->SetAntialiasMode(classic ? D2D1_ANTIALIAS_MODE_ALIASED : D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  rt_->SetTextAntialiasMode(classic ? D2D1_TEXT_ANTIALIAS_MODE_ALIASED : D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
}

void Painter::push_opacity(float opacity) {
  D2D1_LAYER_PARAMETERS params = D2D1::LayerParameters();
  params.opacity = opacity;
  rt_->PushLayer(params, nullptr);
}

void Painter::push_opacity(float opacity, Rect bounds) {
  D2D1_LAYER_PARAMETERS params = D2D1::LayerParameters(D2D1::RectF(bounds.x, bounds.y, bounds.right(), bounds.bottom()));
  params.opacity = opacity;
  rt_->PushLayer(params, nullptr);
}

void Painter::push(const D2D1_MATRIX_3X2_F& m) { rt_->SetTransform(m * base_); }

void Painter::clear(Color c) { rt_->Clear(to_d2d(c)); }

void Painter::fill_rect(Rect r, Color c) {
  brush_->SetColor(to_d2d(c));
  rt_->FillRectangle(D2D1::RectF(r.x, r.y, r.right(), r.bottom()), brush_.get());
}

void Painter::fill_gradient(Rect r, D2D1_POINT_2F from, D2D1_POINT_2F to, std::span<const GradientStop> stops) {
  ID2D1LinearGradientBrush* const brush = gfx_.gradient(rt_, stops);
  if (!brush) return;
  brush->SetStartPoint(from);
  brush->SetEndPoint(to);
  rt_->FillRectangle(D2D1::RectF(r.x, r.y, r.right(), r.bottom()), brush);
}

void Painter::frame(Rect r, Color c, float width) {
  fill_rect({r.x, r.y, r.w, width}, c);
  fill_rect({r.x, r.bottom() - width, r.w, width}, c);
  fill_rect({r.x, r.y + width, width, r.h - 2 * width}, c);
  fill_rect({r.right() - width, r.y + width, width, r.h - 2 * width}, c);
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

const std::array<float, shadow_steps>& shadow_layers() {
  // CSS blur B is a Gaussian of sigma B/2, across a straight edge half of it outside. Each
  // layer adds what the falloff loses across its band; the widest ends at B, about 2 sigma.
  static const std::array<float, shadow_steps> layers = [] {
    const auto outside = [](std::size_t band) {
      const double sigmas = (static_cast<double>(band) + 0.5) * 2 / shadow_steps;
      return 0.5 * std::erfc(sigmas / std::numbers::sqrt2);
    };
    std::array<float, shadow_steps> weights{};
    for (std::size_t i = 0; i < shadow_steps; ++i) {
      weights[i] = static_cast<float>(outside(i) - (i + 1 < shadow_steps ? outside(i + 1) : 0.0));
    }
    return weights;
  }();
  return layers;
}

void Painter::shadow_round(Rect r, float radius, float blur, float dy, Color c) {
  // Layered, progressively larger translucent shapes, stacked into CSS's falloff.
  const auto& layers = shadow_layers();
  for (std::size_t i = layers.size(); i >= 1; --i) {
    const float grow = blur * static_cast<float>(i) / static_cast<float>(layers.size());
    fill_round({r.x - grow, r.y - grow + dy, r.w + 2 * grow, r.h + 2 * grow}, radius + grow, with_alpha(c, c.a * layers[i - 1]));
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

void Painter::emoji(std::wstring_view glyph, Rect r, float opacity) {
  // The glyphs fill less of their line box than the drawn emoji do of theirs.
  const auto layout = gfx_.emoji_layout(glyph, std::min(r.w, r.h) * 0.8f);
  DWRITE_TEXT_METRICS m{};
  if (!layout || FAILED(layout->GetMetrics(&m))) return;
  const bool translucent = opacity < 1;
  // A glyph may overhang its box a little.
  if (translucent) push_opacity(opacity, {r.x - r.w / 2, r.y - r.h / 2, r.w * 2, r.h * 2});
  brush_->SetColor(D2D1::ColorF(0, 0, 0, 1));
  // A picture, whatever the text around it.
  const D2D1_TEXT_ANTIALIAS_MODE text_mode = rt_->GetTextAntialiasMode();
  rt_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  rt_->DrawTextLayout(D2D1::Point2F(r.x + (r.w - m.width) / 2, r.y + (r.h - m.height) / 2), layout.get(), brush_.get(),
                      D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
  rt_->SetTextAntialiasMode(text_mode);
  if (translucent) pop_opacity();
}

void Painter::shadow_geometry(ID2D1Geometry* g, float blur, float dy, Color c) {
  // Layered translucent copies, growing outwards, approximate a Gaussian blur.
  constexpr int steps = 10;
  const D2D1_MATRIX_3X2_F saved = [&] {
    D2D1_MATRIX_3X2_F m;
    rt_->GetTransform(&m);
    return m;
  }();
  ComPtr<ID2D1StrokeStyle> round = round_stroke(gfx_.d2d());
  for (int i = steps; i >= 1; --i) {
    const float grow = blur * static_cast<float>(i) / steps;
    brush_->SetColor(to_d2d(with_alpha(c, c.a * 0.5f / steps)));
    rt_->SetTransform(D2D1::Matrix3x2F::Translation(0, dy) * saved);
    rt_->FillGeometry(g, brush_.get());
    rt_->DrawGeometry(g, brush_.get(), 2 * grow, round.get());
  }
  rt_->SetTransform(saved);
}

void Painter::fill_vertical_gradient(ID2D1Geometry* g, float y0, float y1,
                                     const std::vector<std::pair<float, Color>>& stops) {
  std::vector<D2D1_GRADIENT_STOP> list;
  for (const auto& [offset, color] : stops) list.push_back({offset, to_d2d(color)});
  ComPtr<ID2D1GradientStopCollection> collection;
  ComPtr<ID2D1LinearGradientBrush> gradient;
  if (SUCCEEDED(rt_->CreateGradientStopCollection(list.data(), static_cast<UINT32>(list.size()), collection.put())) &&
      SUCCEEDED(rt_->CreateLinearGradientBrush(
          D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, y0), D2D1::Point2F(0, y1)), collection.get(),
          gradient.put()))) {
    rt_->FillGeometry(g, gradient.get());
  }
}

void Painter::cloud(ID2D1PathGeometry* shape, float x, float y, float scale, float shadow_alpha, float sigma,
                    const Theme& theme) {
  push(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(x, y));
  if (theme.cloud_shadow_scale > 0) {
    // Canvas shadowBlur = sigma * 3 with a 6 px vertical offset (in screen px).
    shadow_geometry(shape, sigma * 3, 6 / scale, with_alpha(theme.cloud_shadow, shadow_alpha));
  }
  fill_geometry(shape, theme.cloud_base);
  fill_vertical_gradient(shape, 8, 318, {{0.0f, theme.cloud_top}, {1.0f, theme.cloud_bottom}});
  rt_->SetTransform(base_);
}

}  // namespace crpp::ui
