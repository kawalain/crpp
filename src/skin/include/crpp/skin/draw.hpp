#pragma once
// Walks a screen of a pack and draws it. What it draws on and what it shows are seams, so that
// the walk is tested without a window.
#include <span>
#include <string_view>

#include "crpp/skin/pack.hpp"

namespace crpp::skin {

struct Box {
  float x = 0, y = 0, w = 0, h = 0;
};

struct TextLook {
  float size = 16, line_height = 0;
  int weight = 400;
  std::uint8_t align = 0;
  bool wrap = false, ellipsis = false, exact = false, keep_words = false;
};

class Canvas {
 public:
  virtual ~Canvas() = default;
  [[nodiscard]] virtual Box snap(Box box) = 0;
  [[nodiscard]] virtual float line() = 0;
  [[nodiscard]] virtual float font_size() = 0;
  [[nodiscard]] virtual float text_width(std::string_view text, const TextLook& look) = 0;
  virtual void fill(Box box, Color color) = 0;
  virtual void fill_down(Box box, std::span<const GradientStop> stops) = 0;
  virtual void fill_round(Box box, float radius, Color color) = 0;
  virtual void fill_round_down(Box box, float radius, std::span<const GradientStop> stops) = 0;
  virtual void frame(Box box, Color color) = 0;
  virtual void stroke_round(Box box, float radius, Color color, float width) = 0;
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

// What a screen shows, by the ids its schema gave the names.
class Source {
 public:
  virtual ~Source() = default;
  // Stays valid until the walk ends.
  [[nodiscard]] virtual std::string_view text(int /*id*/, int /*index*/) { return {}; }
  [[nodiscard]] virtual float number(int /*id*/) { return 0; }
  virtual void picture(int /*id*/, Box /*box*/) {}
  virtual void field(int /*id*/, Box /*box*/, const FieldLook& /*look*/) {}
};

void draw(const Pack& pack, const Screen& screen, int palette, Box area, Canvas& canvas, Source& source);

}  // namespace crpp::skin
