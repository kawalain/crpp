#pragma once
// Controls shared by the screens. They are drawing functions and small value types;
// the surface that uses one keeps its state and registers its hits.
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

// CSS `ease`, for t in 0..1.
[[nodiscard]] float ease(float t);
// How far a transition that began at `start` has come, eased; 1 when animations are off.
[[nodiscard]] float transition(const Ui& ui, unsigned long long start, float duration_ms);
// CSS's `ease`, for t from 0 to 1.
[[nodiscard]] float ease(float t);

// How wide a slider's thumb is: its middle stands at r.x + slider_thumb / 2 + (r.w - slider_thumb) * at.
inline constexpr float slider_thumb = 21;
// Where on a slider (.volume-slider) x is: r is the hit area, the track runs through its middle.
[[nodiscard]] int slider_value(Rect r, float x, int min, int max);

// A one-line field in a box. Its text moves sideways to keep the caret in view, and while a drag
// holds the button past either end it goes on moving that way, faster the farther the pointer is, as
// a scrolled area does under a drag. Every x is in the coordinates the box is painted in.
class LineBox {
 public:
  // Draws `field` clipped to `box` and returns the caret. `colors` are the text, the placeholder,
  // the caret, the selection and the background; `held` says the button is down on the field.
  Rect paint(Ui& ui, Painter& p, TextField& field, Rect box, const TextStyle& style, std::wstring_view placeholder,
             std::span<const Color> colors, bool focused, bool held);
  // The place in the text under x.
  [[nodiscard]] std::size_t position_at(const Ui& ui, const TextField& field, float x) const {
    return field.position_at(ui.gfx, style_, x - box_.x + scroll_, 0);
  }
  // A drag to x. Past an end the selection stops at the edge, and the frames move the text on.
  void drag(const Ui& ui, TextField& field, float x);
  void reset() { scroll_ = 0, moved_at_ = 0; }

 private:
  TextStyle style_{};
  Rect box_{};
  float scroll_ = 0, drag_x_ = 0;  // how far the text is moved left; where the drag is
  unsigned long long moved_at_ = 0;
};

// A scrolled column of content; the owner draws it at y - drawn(). The wheel sets where
// it heads and each frame's advance moves it there. Past either end it stretches a little
// and comes back. Upstream stops dead there; this is a difference on purpose.
struct Scroll {
  float offset = 0;  // where it is on the way to target, always within the content
  float content_h = 0;
  float view_h = 0;
  float target = 0;   // where the wheel sent it
  float stretch = 0;  // how far it is pulled past an end: below 0 above the top
  unsigned long long moved_at = 0, pulled_at = 0;

  float edge_fade = 27;  // how long the edge fades can be, from what last moved it

  [[nodiscard]] float max_offset() const { return content_h > view_h ? content_h - view_h : 0; }
  [[nodiscard]] float drawn() const { return offset + stretch; }
  [[nodiscard]] bool moving() const { return offset != target || stretch != 0; }
  // The content may have changed: everything stays within it.
  void clamp();
  void jump_to(float at);
  void wheel(const Ui& ui, float delta, float px_per_notch = 80);
  // Once per frame, before drawing. Asks for another frame while it moves.
  void advance(Ui& ui);
  // How long the edge fades should be now: nothing at the very end of the content, the
  // full length a little way in, so they grow with the distance and do not pop in.
  [[nodiscard]] float fade_top() const;
  [[nodiscard]] float fade_bottom() const;
  [[nodiscard]] Rect thumb(Rect track) const;
  // A click on a scrollbar with arrows: a line on an arrow, beyond the track, and a page
  // beside the thumb.
  void step_towards(Rect track, float y, float line = 20);
  // Moves so the thumb's middle is at y.
  void drag_thumb(Rect track, float y);
  // Moves to a place with the wheel's motion, and no stretch.
  void go_to(const Ui& ui, float at);
  // Up and Down a line, Page Up and Down most of the view, Home and End the ends. True if it was one of them.
  bool key(const Ui& ui, unsigned vk, float line);
};

// What a key does to a slider at value, from min to max: the arrows a step, Page Up and Down a tenth of
// the way, Home and End the ends. Nothing for another key, or one that would not move it.
[[nodiscard]] std::optional<int> slider_key(unsigned vk, int value, int min, int max);

// A pick list (.dropdown). Hit ids are id_base for the button and id_base + 1 + i for
// each option, so a surface reserves a range. Draw the list last, outside any clip.
// Everything is in stage coordinates.
class Dropdown {
 public:
  std::vector<std::string> options;
  // Parallel to options, or empty: which of them head the ones after them. A heading is drawn
  // between lines, takes no clicks and is never selected.
  std::vector<bool> headings;
  int selected = 0;
  bool open = false;
  bool up = false;  // the list opens above the button
  bool disabled = false;
  int id_base = 0;

  [[nodiscard]] static skin::ScreenSchema schema();

  void paint_button(Ui& ui, Surface* owner, Painter& p, Rect r) const;
  void paint_list(Ui& ui, Surface* owner, Painter& p, Rect r);
  // A click on one of this dropdown's ids. True if `selected` changed.
  bool click(int id);
  // True if the wheel was over the open list.
  bool wheel(const Ui& ui, float x, float y, float delta);
  // A key while the keyboard is on it. Closed, Space, F4 and Alt with an arrow open it, and the arrows,
  // Home and End pick another; open, they move the option the keyboard is on, drawn as the pointer's,
  // and Enter or Space takes it. The answer is the id a click would have hit, to act on as a click:
  // taken_alone when the key did all there is, and nothing when it is not the dropdown's.
  static constexpr int taken_alone = -1;
  [[nodiscard]] std::optional<int> key(Ui& ui, Surface* owner, unsigned vk, bool alt);
  [[nodiscard]] bool owns(int id) const { return id >= id_base && id <= id_base + static_cast<int>(options.size()); }
  [[nodiscard]] bool heading(std::size_t index) const { return index < headings.size() && headings[index]; }

 private:
  Rect list_;  // stage coordinates
  Scroll scroll_;
  int active_ = -1;  // the option the keyboard is on while open
};

// What is cut off at the top or bottom of a list fades out over the given lengths, 27 px
// at most (.scroll-area's mask). Returns whether it pushed a layer, as it does not when
// neither edge cuts anything. Pair with pop_edge_fade.
inline constexpr float edge_fade_px = 27;
[[nodiscard]] bool push_edge_fade(Ui& ui, Painter& p, Rect area, float top, float bottom);
inline void pop_edge_fade(Painter& p, bool pushed) {
  if (pushed) p.pop_opacity();
}

inline constexpr float modal_pop_ms = 160;

}  // namespace crpp::ui
