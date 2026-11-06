#pragma once
// A small label over something (.tip, .copy-tip, .link-tip, .volume-tooltip): it fades
// in and out on its own clock, so the owner only says each frame what should show.
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

// A CSS keyframe track at t in 0..1: stops are (offset, value), eased between each.
[[nodiscard]] float keyframes(float t, std::span<const std::pair<float, float>> stops);

class HoverTip {
 public:
  enum class Place : std::uint8_t { Center, Left, Right };  // which edge of the anchor it lines up with

  struct Spec {
    std::string text;  // '\n' breaks lines
    Rect anchor;       // window coordinates; the label sits `gap` above it
    Place place = Place::Center;
    float size = 16;
    float gap = 8;
    float slide = 4;  // how far below its place it starts
    float in_ms = 150, out_ms = 150;
    float pad_x = 15, pad_y = 5, radius = 11, line_height = 22;
    int weight = 700;
    std::optional<Color> fill, ink;  // default: the accent color
    float dx = 0;                    // an added shift, for a shake
  };

  // Call every frame with what should show, nullptr for nothing. A different text
  // starts a new fade in.
  void paint(Ui& ui, Painter& p, const Spec* want);
  // True while fading, and until the next frame after the hover moved, since only a
  // frame can tell what the new hover shows.
  [[nodiscard]] bool animating(const Ui& ui) const;
  [[nodiscard]] bool visible() const { return shown_; }
  [[nodiscard]] const std::string& text() const { return spec_.text; }

 private:
  Spec spec_;
  bool shown_ = false;
  unsigned long long since_ = 0;
  const Surface* seen_owner_ = nullptr;
  int seen_id_ = 0;
};

}  // namespace crpp::ui
