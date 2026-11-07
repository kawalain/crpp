#pragma once
// A Skin Pack's walk drawn on a Painter, with its hits going to one Surface.
#include <memory>

#include "crpp/skin/draw.hpp"
#include "crpp/ui/surface.hpp"

namespace crpp::ui {

[[nodiscard]] TextStyle text_style_of(const skin::TextLook& look);
[[nodiscard]] inline Rect rect_of(skin::Box box) { return {box.x, box.y, box.w, box.h}; }

// A Skin whose controls are the parts of the pack in use.
[[nodiscard]] std::unique_ptr<Skin> make_pack_skin(Ui& ui);

// A screen walked on a Painter, with its hits going to `owner`, which may be nothing.
void draw_screen(Ui& ui, Painter& p, Surface* owner, const skin::Screen& screen, Rect area, skin::Source& source);
// The same walk with nothing drawn, for what the screen reports about its size.
void measure_screen(const Ui& ui, const skin::Screen& screen, Rect area, skin::Source& source);

class SkinCanvas final : public skin::Canvas {
 public:
  // hit_base is added to every id, for a list of which a surface has several.
  SkinCanvas(Ui& ui, Painter& painter, Surface* owner, int hit_base = 0) : ui_(ui), p_(painter), owner_(owner), hit_base_(hit_base) {}

  skin::Box snap(skin::Box box) override;
  float snap(float value) override;
  float line() override { return p_.line(); }
  float font_size() override { return p_.gfx().classic_size(); }
  float text_width(std::string_view text, const skin::TextLook& look, float width) override;
  void fill(skin::Box box, skin::Color color) override { p_.fill_rect(rect_of(box), color); }
  float text_height(std::string_view text, const skin::TextLook& look, float width) override;
  void fill_down(skin::Box box, float y0, float y1, std::span<const skin::GradientStop> stops) override { p_.fill_gradient(rect_of(box), {box.x, y0}, {box.x, y1}, stops); }
  void fill_round(skin::Box box, float radius, skin::Color color) override { p_.fill_round(rect_of(box), radius, color); }
  void fill_round_down(skin::Box box, float radius, float y0, float y1, std::span<const skin::GradientStop> stops) override { p_.fill_round_gradient(rect_of(box), radius, {box.x, y0}, {box.x, y1}, stops); }
  void frame(skin::Box box, skin::Color color) override { p_.frame(rect_of(box), color); }
  void stroke_round(skin::Box box, float radius, skin::Color color, float width) override { p_.stroke_round(rect_of(box), radius, color, width); }
  void line(float x1, float y1, float x2, float y2, skin::Color color, float width) override { p_.stroke_line(x1, y1, x2, y2, color, width); }
  void path(std::string_view data, float x, float y, float scale, skin::Color color, float width, bool round_caps, bool soft) override;
  void text(std::string_view text, skin::Box box, const skin::TextLook& look, skin::Color color) override { p_.text(text, rect_of(box), text_style_of(look), color); }
  void push_clip(skin::Box box) override;
  void pop_clip() override;
  void hit(skin::Box box, int id) override { ui_.add_hit(owner_, rect_of(box), hit_base_ + id); }
  bool hot(int id) override { return ui_.hot(owner_, hit_base_ + id); }
  bool pressed(int id) override { return ui_.pressed(owner_, hit_base_ + id); }

 private:
  Ui& ui_;
  Painter& p_;
  Surface* owner_;
  int hit_base_;
};

}  // namespace crpp::ui
