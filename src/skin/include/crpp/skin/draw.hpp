#pragma once
// Walks a screen of a pack and draws it. What it draws on and what it shows are seams, so that
// the walk is tested without a window.
#include <span>
#include <string_view>
#include <utility>

#include "crpp/skin/pack.hpp"

namespace crpp::skin {

struct Box {
  float x = 0, y = 0, w = 0, h = 0;
};

struct TextLook {
  float size = 16, line_height = 0, letter_spacing = 0;
  int weight = 400;
  std::uint8_t align = 0, role = 0;
  bool wrap = false, ellipsis = false, exact = false, keep_words = false;
};

class Canvas {
 public:
  virtual ~Canvas() = default;
  [[nodiscard]] virtual Box snap(Box box) = 0;
  [[nodiscard]] virtual float snap(float value) = 0;
  [[nodiscard]] virtual float line() = 0;
  [[nodiscard]] virtual float font_size() = 0;
  [[nodiscard]] virtual float text_width(std::string_view text, const TextLook& look, float width) = 0;
  [[nodiscard]] virtual float text_height(std::string_view text, const TextLook& look, float width) = 0;
  virtual void fill(Box box, Color color) = 0;
  // A gradient straight down from y0 to y1, on the window.
  virtual void fill_down(Box box, float y0, float y1, std::span<const GradientStop> stops) = 0;
  virtual void fill_round(Box box, float radius, Color color) = 0;
  virtual void fill_round_down(Box box, float radius, float y0, float y1, std::span<const GradientStop> stops) = 0;
  virtual void frame(Box box, Color color) = 0;
  virtual void stroke_round(Box box, float radius, Color color, float width) = 0;
  virtual void line(float x1, float y1, float x2, float y2, Color color, float width) = 0;
  // Filled when width is 0.
  virtual void path(std::string_view data, float x, float y, float scale, Color color, float width, bool round_caps, bool soft) = 0;
  virtual void text(std::string_view text, Box box, const TextLook& look, Color color) = 0;
  virtual void push_clip(Box box) = 0;
  virtual void pop_clip() = 0;
  virtual void hit(Box box, int id) = 0;
  [[nodiscard]] virtual bool hot(int id) = 0;
  [[nodiscard]] virtual bool pressed(int id) = 0;
};

struct FieldLook {
  TextLook text;
  Color color, placeholder_color, caret, selection;
  std::string_view placeholder;
};

struct SlotValues {
  std::span<const float> numbers;
  std::span<const TextLook> styles;
  std::span<const Color> colors;
};

// What a screen shows, by the ids its schema gave the names. `index` is the round of the nearest repeat.
class Source {
 public:
  virtual ~Source() = default;
  // Stays valid until the walk ends.
  [[nodiscard]] virtual std::string_view text(int /*id*/, int /*index*/) { return {}; }
  [[nodiscard]] virtual float number(int /*id*/, int /*index*/) { return 0; }
  virtual void picture(int /*id*/, int /*index*/, Box /*box*/, float /*opacity*/, float /*gray*/) {}
  virtual void field(int /*id*/, int /*index*/, Box /*box*/, const FieldLook& /*look*/) {}
  virtual void slot(int /*id*/, int /*index*/, Box /*box*/, const SlotValues& /*values*/) {}
  virtual void report(int /*id*/, float /*value*/) {}
  virtual void note(int /*id*/, int /*index*/, Box /*box*/) {}
};

void draw(const Pack& pack, const Screen& screen, int palette, Box area, Canvas& canvas, Source& source);

// One part, for what a screen's code draws itself.
struct PartCall {
  std::string_view label;
  bool hot = false, pressed = false;
  std::span<const std::pair<std::string_view, float>> args;
};
void draw_part(const Pack& pack, const Part& part, int palette, Box box, Canvas& canvas, Source& source, const PartCall& call);

}  // namespace crpp::skin
