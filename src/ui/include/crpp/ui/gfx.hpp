#pragma once
// Direct2D / DirectWrite drawing helpers shared by every screen (Windows only).
#include <d2d1.h>
#include <dwrite.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
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

struct TextStyle {
  float size = 16;
  int weight = 400;
  float line_height = 0;  // CSS line-height in px; 0 = the font's natural height
  Align align = Align::Left;
  bool wrap = false;
  bool ellipsis = false;
  float letter_spacing = 0;  // px added after each character
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
  void set_language(std::wstring tag) { language_ = std::move(tag); }

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

  void clear(Color c);
  void fill_rect(Rect r, Color c);
  void fill_round(Rect r, float radius, Color c);
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
  // The upstream cloud shape (see main.js drawCloud) at x,y with the given scale.
  void cloud(ID2D1PathGeometry* shape, float x, float y, float scale, float shadow_alpha, float sigma,
             const Theme& theme);

 private:
  const Gfx& gfx_;
  ID2D1RenderTarget* rt_;
  ComPtr<ID2D1SolidColorBrush> brush_;
  D2D1_MATRIX_3X2_F base_ = D2D1::Matrix3x2F::Identity();
  D2D1_MATRIX_3X2_F moved_ = D2D1::Matrix3x2F::Identity();
};

// Upstream cloud outline (client/src/main.js cloudPath), 0..492 x 8..318.
extern const char* const cloud_path_data;

}  // namespace crpp::ui
