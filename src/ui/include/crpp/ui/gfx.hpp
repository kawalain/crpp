#pragma once
// Direct2D / DirectWrite drawing helpers shared by every screen (Windows only).
#include <d2d1.h>
#include <dwrite_2.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "crpp/locale/locale.hpp"
#include "crpp/ui/com.hpp"
#include "crpp/ui/theme.hpp"

namespace crpp::ui {

using locale::tr;

struct Rect {
  float x = 0, y = 0, w = 0, h = 0;
  [[nodiscard]] bool contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
  [[nodiscard]] float right() const { return x + w; }
  [[nodiscard]] float bottom() const { return y + h; }
};

enum class Align : std::uint8_t { Left, Center, Right };
enum class FontRole : std::uint8_t { Body, Caption };

struct TextStyle {
  float size = 16;
  int weight = 400;
  float line_height = 0;  // CSS line-height in px; 0 = the font's natural height
  Align align = Align::Left;
  bool wrap = false;
  bool ellipsis = false;
  float letter_spacing = 0;  // px added after each character
  // With classic fonts a size means a kind of text and not its pixels, unless this says so.
  bool exact = false;
  FontRole role = FontRole::Body;
  // With classic fonts: a line of Hangul breaks only between words. Character positions in the
  // layout then differ from the text's, so it is not for text with ranges or a caret.
  bool keep_words = false;
};

std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view wide);

class Gfx {
 public:
  // font_file: an optional TTF/OTF (a variable font works) used as the UI font;
  // falls back to Malgun Gothic if it cannot be loaded.
  bool init(const std::filesystem::path& font_file = {});
  void load_font(const std::filesystem::path& font_file);

  // Made again if it was released. Null only if Direct2D itself fails.
  [[nodiscard]] ID2D1Factory* d2d() const;
  [[nodiscard]] IDWriteFactory* dwrite() const { return dwrite_.get(); }

  // SVG path data (M L H V C S A Z, absolute and relative) as a geometry.
  [[nodiscard]] ComPtr<ID2D1PathGeometry> parse_path(std::string_view d) const;
  // Picks glyph variants, as a page's language does: "ko-kr".
  void set_language(std::wstring tag);
  // System fonts drawn aliased, with their pixels' own widths, as on old Windows: Gulim or
  // Tahoma, and Trebuchet MS in a caption. The app's font is let go of until this goes off.
  void set_classic(bool on);
  // Which fonts classic text uses: the first of each list this system has.
  struct ClassicFonts {
    std::vector<std::wstring> korean = {L"Gulim", L"Malgun Gothic"}, other = {L"Tahoma"}, caption = {L"Trebuchet MS"};
    float korean_size = 12, other_size = 11;
  };
  void set_classic_fonts(ClassicFonts fonts);
  [[nodiscard]] bool classic() const { return classic_; }
  // The classic body size for this language, in DIPs.
  [[nodiscard]] float classic_size() const { return classic_size_; }
  // Pixels per DIP of what is being drawn on; classic widths depend on it.
  void set_pixels_per_dip(float scale) { pixels_per_dip_ = scale; }
  // A lasting brush for these stops, which must stay where they are. Set its points to use it.
  [[nodiscard]] ID2D1LinearGradientBrush* gradient(ID2D1RenderTarget* rt, std::span<const GradientStop> stops) const;
  void forget_render_target() const;

  // The same, kept here until release_factory. Geometry that lasts beyond a frame lives
  // here, so that nothing else can hold the factory alive.
  [[nodiscard]] ID2D1PathGeometry* path(std::string_view d) const;
  // The factory holds the GPU device, and with it the driver's memory, until it goes.
  // Every resource made from it must be gone too; App::release_graphics sees to that.
  void release_factory();

  // The text of an emoji id ("1f408-200d-2b1b"), if the system's emoji font shows it as
  // one glyph; otherwise empty.
  [[nodiscard]] std::wstring emoji_glyph(std::string_view id) const;
  [[nodiscard]] ComPtr<IDWriteTextLayout> emoji_layout(std::wstring_view text, float size) const;

  // A laid-out text block. max_w/max_h bound wrapping and alignment.
  [[nodiscard]] ComPtr<IDWriteTextLayout> layout(std::wstring_view text, const TextStyle& style, float max_w,
                                                 float max_h = 10000) const;

 private:
  mutable ComPtr<ID2D1Factory> d2d_;
  mutable std::map<std::string, ComPtr<ID2D1PathGeometry>, std::less<>> paths_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<IDWriteFontCollection> fonts_;  // null = the system collection
  std::filesystem::path font_file_;
  bool classic_ = false;
  float classic_size_ = 12, pixels_per_dip_ = 1;
  ClassicFonts classic_fonts_;
  std::wstring classic_body_, classic_caption_;
  ComPtr<IDWriteFontFallback> classic_fallback_;
  struct Metrics {
    float ascent = 0.8f, descent = 0.2f;
  };
  Metrics classic_body_metrics_, classic_caption_metrics_;
  void choose_classic_fonts();
  [[nodiscard]] Metrics metrics_of(const std::wstring& family) const;
  void apply_style(IDWriteTextFormat* format, const TextStyle& style, Metrics metrics) const;
  void finish_layout(IDWriteTextLayout* layout, IDWriteTextFormat* format, std::wstring_view text, const TextStyle& style) const;
  [[nodiscard]] ComPtr<IDWriteTextLayout> classic_layout(std::wstring_view text, TextStyle style, float max_w, float max_h) const;
  mutable std::map<const void*, ComPtr<ID2D1LinearGradientBrush>> gradients_;
  mutable ID2D1RenderTarget* gradients_target_ = nullptr;
  std::wstring family_;
  std::wstring language_ = L"ko-kr";
  float ascent_ = 0.8f;   // of the em, for the UI font
  float descent_ = 0.2f;
};

// Draws into one render target. Cheap to create each frame.
// How much each widening layer of a box shadow adds, from the narrowest, as a share of its
// colour's alpha.
inline constexpr std::size_t shadow_steps = 12;
[[nodiscard]] const std::array<float, shadow_steps>& shadow_layers();

class Painter {
 public:
  Painter(const Gfx& gfx, ID2D1RenderTarget* rt);

  [[nodiscard]] const Gfx& gfx() const { return gfx_; }
  [[nodiscard]] ID2D1RenderTarget* rt() const { return rt_; }

  // With classic fonts every box and line sits on whole pixels at any scale, so that equal
  // sizes come out equal wherever they land and lines stay sharp. Curves are still smoothed.
  // Where r is drawn: it keeps its size in pixels, and fill_rect and frame already do this.
  [[nodiscard]] Rect snap(Rect r) const;
  // One line's thickness in DIPs: a whole number of pixels.
  [[nodiscard]] float line() const { return line_; }

  void set_base(const D2D1_MATRIX_3X2_F& m) {
    base_ = m * moved_;
    rt_->SetTransform(base_);
  }
  // Moves all that is drawn next, whatever base it sets, for a screen that slides in.
  void set_moved(float dx, float dy) { moved_ = D2D1::Matrix3x2F::Translation(dx, dy); }
  // Everything until pop_opacity is drawn as one, then blended at this opacity. Direct2D
  // keeps a buffer as large as the layer's bounds in its pool until the device goes, so
  // anything smaller than the window says how far it can reach. What falls outside is cut.
  void push_opacity(float opacity);
  void push_opacity(float opacity, Rect bounds);
  void pop_opacity() { rt_->PopLayer(); }
  // Applies m (in stage coordinates) on top of the base transform.
  void push(const D2D1_MATRIX_3X2_F& m);
  void pop() { rt_->SetTransform(base_); }
  // Until pop_group everything goes through m as well, with what push does included.
  void push_group(const D2D1_MATRIX_3X2_F& m);
  void pop_group();

  void clear(Color c);
  void fill_rect(Rect r, Color c);
  // A gradient across r, from the given corner to the other way the line runs. The stops
  // must stay where they are: the brush is kept for them.
  void fill_gradient(Rect r, D2D1_POINT_2F from, D2D1_POINT_2F to, std::span<const GradientStop> stops);
  void fill_down(Rect r, std::span<const GradientStop> stops) {
    fill_gradient(r, {r.x, r.y}, {r.x, r.bottom()}, stops);
  }
  // A line inside r's edge.
  void frame(Rect r, Color c, float width = 1);
  void fill_round(Rect r, float radius, Color c);
  void fill_round_gradient(Rect r, float radius, D2D1_POINT_2F from, D2D1_POINT_2F to, std::span<const GradientStop> stops);
  void fill_round_down(Rect r, float radius, std::span<const GradientStop> stops) {
    fill_round_gradient(r, radius, {r.x, r.y}, {r.x, r.bottom()}, stops);
  }
  // A line of this thickness inside the edge of a rounded box.
  void stroke_round(Rect r, float radius, Color c, float width);
  void fill_ellipse(float cx, float cy, float rx, float ry, Color c);
  void fill_geometry(ID2D1Geometry* g, Color c);
  void stroke_geometry(ID2D1Geometry* g, Color c, float width, bool round_caps = true);
  void stroke_line(float x1, float y1, float x2, float y2, Color c, float width);
  void stroke_ellipse(float cx, float cy, float rx, float ry, Color c, float width);
  // A soft shadow under a rounded rectangle (card and popup shadows).
  // As CSS box-shadow with this blur and offset.
  void shadow_round(Rect r, float radius, float blur, float dy, Color c);

  void draw_layout(IDWriteTextLayout* layout, float x, float y, Color c);
  // Draws text with its CSS line box starting at r.y. Returns the laid-out size.
  D2D1_SIZE_F text(std::wstring_view s, Rect r, const TextStyle& style, Color c);
  // A colour glyph of the system's emoji font, filling r.
  void emoji(std::wstring_view glyph, Rect r, float opacity);
  D2D1_SIZE_F text(std::string_view utf8, Rect r, const TextStyle& style, Color c) {
    return text(widen(utf8), r, style, c);
  }

  // A soft shadow under any shape, drawn with the current transform.
  void shadow_geometry(ID2D1Geometry* g, float blur, float dy, Color c);
  // A vertical gradient fill over y0..y1 with the given stops (offset, color).
  void fill_vertical_gradient(ID2D1Geometry* g, float y0, float y1,
                              const std::vector<std::pair<float, Color>>& stops);

 private:
  const Gfx& gfx_;
  ID2D1RenderTarget* rt_;
  ComPtr<ID2D1SolidColorBrush> brush_;
  bool classic_ = false;
  float pixels_ = 1, line_ = 1;
  D2D1_MATRIX_3X2_F base_ = D2D1::Matrix3x2F::Identity();
  D2D1_MATRIX_3X2_F moved_ = D2D1::Matrix3x2F::Identity();
  static constexpr std::size_t most_groups = 8;
  std::array<D2D1_MATRIX_3X2_F, most_groups> groups_{};
  std::size_t group_depth_ = 0;
};

}  // namespace crpp::ui
