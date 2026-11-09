#pragma once
// A small SVG renderer for the emoji assets (Twemoji-style files: path, circle,
// ellipse and g with fill, stroke and transform attributes). Not a general SVG
// implementation.
#include <filesystem>
#include <list>
#include <string>
#include <string_view>
#include <vector>

#include "crpp/files/tree.hpp"
#include "crpp/ui/gfx.hpp"

namespace crpp::ui {

struct SvgImage {
  struct Shape {
    ComPtr<ID2D1Geometry> geometry;
    D2D1_MATRIX_3X2_F matrix = D2D1::Matrix3x2F::Identity();
    bool fill = true;
    Color fill_color{0, 0, 0, 1};
    bool stroke = false;
    Color stroke_color{0, 0, 0, 1};
    float stroke_width = 1;
  };
  float width = 36, height = 36;  // viewBox size
  std::vector<Shape> shapes;
  std::wstring glyph;  // drawn with the system's emoji font instead, when not empty
  [[nodiscard]] bool empty() const { return shapes.empty() && glyph.empty(); }
};

// Parses SVG text; returns an empty image (no shapes) on failure.
SvgImage parse_svg(const Gfx& gfx, std::string_view svg);

// Draws the image scaled to fit dst, optionally translucent.
// `gray` from 0 to 1 is CSS's grayscale(). A colour font's glyph cannot take it.
void draw_svg(Painter& p, const SvgImage& image, Rect dst, float opacity = 1.0f, float gray = 0);

// Whether an id can name an emoji file. Ids come from other members through the server,
// and become part of a path.
[[nodiscard]] bool valid_emoji_id(std::string_view id) noexcept;

// Loads emoji files by id ("1f600") from the assets' emoji folder, keeping the most
// recently used few parsed. An emoji whose file is absent comes from the system's emoji
// font if that can show it.
class EmojiCache {
 public:
  explicit EmojiCache(files::Tree assets, std::size_t capacity = 48) : files_(std::move(assets)), capacity_(capacity) {}
  // Never null; an unknown id yields an empty image.
  const SvgImage& get(const Gfx& gfx, const std::string& id);
  [[nodiscard]] const files::Tree& files() const { return files_; }
  // Whether get would find something to show, without parsing it.
  [[nodiscard]] bool available(const Gfx& gfx, const std::string& id) const;
  [[nodiscard]] std::size_t parsed() const { return entries_.size(); }

 private:
  struct Entry {
    std::string id;
    SvgImage image;
  };
  files::Tree files_;
  std::list<Entry> entries_;  // most recently used first
  std::size_t capacity_;
};

}  // namespace crpp::ui
