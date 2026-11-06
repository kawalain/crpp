#pragma once
// A small SVG renderer for the emoji assets (Twemoji-style files: path, circle,
// ellipse and g with fill, stroke and transform attributes). Not a general SVG
// implementation.
#include <filesystem>
#include <list>
#include <string>
#include <string_view>
#include <vector>

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
};

// Parses SVG text; returns an empty image (no shapes) on failure.
SvgImage parse_svg(const Gfx& gfx, std::string_view svg);

// Draws the image scaled to fit dst, optionally translucent.
void draw_svg(Painter& p, const SvgImage& image, Rect dst, float opacity = 1.0f);

// Whether an id can name an emoji file. Ids come from other members through the server,
// and become part of a path.
[[nodiscard]] bool valid_emoji_id(std::string_view id) noexcept;

// Loads emoji files by id ("1f600") from the asset folders, keeping the most
// recently used few parsed.
class EmojiCache {
 public:
  explicit EmojiCache(std::filesystem::path assets_dir, std::size_t capacity = 48)
      : dir_(std::move(assets_dir)), capacity_(capacity) {}
  // Never null; an unknown id yields an empty image.
  const SvgImage& get(const Gfx& gfx, const std::string& id);
  [[nodiscard]] const std::filesystem::path& dir() const { return dir_; }
  [[nodiscard]] std::size_t parsed() const { return entries_.size(); }

 private:
  struct Entry {
    std::string id;
    SvgImage image;
  };
  std::filesystem::path dir_;
  std::list<Entry> entries_;  // most recently used first
  std::size_t capacity_;
};

}  // namespace crpp::ui
