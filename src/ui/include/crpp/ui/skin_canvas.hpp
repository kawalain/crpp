#pragma once
// A Skin Pack's walk drawn on a Painter, with its hits going to one Surface.
#include "crpp/skin/draw.hpp"
#include "crpp/ui/surface.hpp"

namespace crpp::ui {

[[nodiscard]] TextStyle text_style_of(const skin::TextLook& look);
[[nodiscard]] inline Rect rect_of(skin::Box box) { return {box.x, box.y, box.w, box.h}; }

class SkinCanvas final : public skin::Canvas {
 public:
  SkinCanvas(Ui& ui, Painter& painter, Surface* owner) : ui_(ui), p_(painter), owner_(owner) {}

  skin::Box snap(skin::Box box) override;
  float line() override { return p_.line(); }
  float font_size() override { return p_.gfx().classic_size(); }
  float text_width(std::string_view text, const skin::TextLook& look) override;
  void fill(skin::Box box, skin::Color color) override { p_.fill_rect(rect_of(box), color); }
  void fill_down(skin::Box box, std::span<const skin::GradientStop> stops) override { p_.fill_down(rect_of(box), stops); }
  void fill_round(skin::Box box, float radius, skin::Color color) override { p_.fill_round(rect_of(box), radius, color); }
  void fill_round_down(skin::Box box, float radius, std::span<const skin::GradientStop> stops) override { p_.fill_round_down(rect_of(box), radius, stops); }
  void frame(skin::Box box, skin::Color color) override { p_.frame(rect_of(box), color); }
  void stroke_round(skin::Box box, float radius, skin::Color color, float width) override { p_.stroke_round(rect_of(box), radius, color, width); }
  void text(std::string_view text, skin::Box box, const skin::TextLook& look, skin::Color color) override { p_.text(text, rect_of(box), text_style_of(look), color); }
  void push_clip(skin::Box box) override;
  void pop_clip() override;
  void hit(skin::Box box, int id) override { ui_.add_hit(owner_, rect_of(box), id); }
  bool hot(int id) override { return ui_.hot(owner_, id); }
  bool pressed(int id) override { return ui_.pressed(owner_, id); }

 private:
  Ui& ui_;
  Painter& p_;
  Surface* owner_;
};

}  // namespace crpp::ui
