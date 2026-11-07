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
// A screen in the window's pixels, over the stage, as a modal and a toast are: its hits go there too.
void draw_layer(Ui& ui, Painter& p, Surface* owner, const skin::Screen& screen, Rect area, skin::Source& source);
// The same walk with nothing drawn, for what the screen reports about its size.
void measure_screen(const Ui& ui, const skin::Screen& screen, Rect area, skin::Source& source);

class SkinCanvas final : public skin::Canvas {
 public:
  // hit_base is added to every id, for a list of which a surface has several.
  SkinCanvas(Ui& ui, Painter& painter, Surface* owner, int hit_base = 0) : ui_(ui), p_(painter), owner_(owner), hit_base_(hit_base) {}
  // What is drawn is in the window's pixels and not on the stage, as a modal is.
  void in_window_coordinates() { in_window_ = true; }
  // Whose its transitions are, when that is not the owner of its hits.
  void motion_of(const void* whose) { whose_ = whose; }

  skin::Box snap(skin::Box box) override;
  float snap(float value) override;
  float line() override { return p_.line(); }
  float font_size() override { return p_.gfx().classic_size(); }
  float text_width(std::string_view text, const skin::TextLook& look, float width) override;
  void fill(skin::Box box, skin::Color color) override { p_.fill_rect(rect_of(box), color); }
  float text_height(std::string_view text, const skin::TextLook& look, float width) override;
  void fill_down(skin::Box box, float y0, float y1, std::span<const skin::GradientStop> stops) override { p_.fill_gradient(rect_of(box), {box.x, y0}, {box.x, y1}, stops); }
  void fill_across(skin::Box box, std::span<const skin::GradientStop> stops) override { p_.fill_gradient(rect_of(box), {box.x, box.y}, {box.x + box.w, box.y}, stops); }
  void fill_round(skin::Box box, float radius, skin::Color color) override { p_.fill_round(rect_of(box), radius, color); }
  void fill_round_down(skin::Box box, float radius, float y0, float y1, std::span<const skin::GradientStop> stops) override { p_.fill_round_gradient(rect_of(box), radius, {box.x, y0}, {box.x, y1}, stops); }
  void frame(skin::Box box, skin::Color color) override { p_.frame(rect_of(box), color); }
  void stroke_round(skin::Box box, float radius, skin::Color color, float width) override { p_.stroke_round(rect_of(box), radius, color, width); }
  void line(float x1, float y1, float x2, float y2, skin::Color color, float width) override { p_.stroke_line(x1, y1, x2, y2, color, width); }
  void path(std::string_view data, float x, float y, float scale, skin::Color color, float width, bool round_caps, bool soft) override;
  void text(std::string_view text, skin::Box box, const skin::TextLook& look, skin::Color color) override { p_.text(text, rect_of(box), text_style_of(look), color); }
  void push_clip(skin::Box box) override;
  void pop_clip() override;
  void hit(skin::Box box, int id) override {
    if (in_window_) ui_.add_hit_window(owner_, rect_of(box), hit_base_ + id);
    else ui_.add_hit(owner_, rect_of(box), hit_base_ + id);
  }
  void hit_shape(skin::Box box, int id, std::string_view data, float x, float y) override { ui_.add_hit_shape(owner_, rect_of(box), hit_base_ + id, data, x, y); }
  float transition(std::uint64_t key, const skin::TransitionSpec& spec) override;
  void fill_ellipse(skin::Box box, skin::Color color) override { p_.fill_ellipse(box.x + box.w / 2, box.y + box.h / 2, box.w / 2, box.h / 2, color); }
  void stroke_ellipse(skin::Box box, skin::Color color, float width) override { p_.stroke_ellipse(box.x + box.w / 2, box.y + box.h / 2, box.w / 2, box.h / 2, color, width); }
  void shadow_round(skin::Box box, float radius, float blur, float dy, skin::Color color) override { p_.shadow_round(rect_of(box), radius, blur, dy, color); }
  void path_gradient(std::string_view data, float x, float y, float scale, float y0, float y1, std::span<const skin::GradientStop> stops) override;
  void path_shadow(std::string_view data, float x, float y, float scale, float blur, float dy, skin::Color color) override;
  void push_scale(float x, float y, float scale) override { p_.push_group(scale_about(x, y, scale)); }
  void pop_scale() override { p_.pop_group(); }
  bool push_opacity(skin::Box box, float opacity) override;
  bool push_edge_fade(skin::Box box, float top, float bottom) override;
  void pop_layer() override { p_.pop_opacity(); }
  bool hot(int id) override { return ui_.hot(owner_, hit_base_ + id); }
  bool pressed(int id) override { return ui_.pressed(owner_, hit_base_ + id); }

 private:
  Ui& ui_;
  Painter& p_;
  Surface* owner_;
  int hit_base_;
  const void* whose_ = nullptr;
  bool in_window_ = false;
};

}  // namespace crpp::ui
