#pragma once
// A small label over something (.tip, .copy-tip, .link-tip, .volume-tooltip): it fades
// in and out on its own clock, so the owner only says each frame what should show.
#include <string>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

class HoverTip {
 public:
  enum class Place : std::uint8_t { Center, Left, Right };  // which edge of the anchor it lines up with
  // Which of a pack's labels this is.
  enum class Look : std::uint8_t { Plain, Small, Copied, Link, Volume, Member };

  struct Spec {
    std::string text;  // '\n' breaks lines
    Rect anchor;       // window coordinates; the label sits `gap` above it
    Place place = Place::Center;
    Look look = Look::Plain;
    float gap = 8;
    float dx = 0;          // an added shift
    unsigned shakes = 0;   // a new value shakes it
  };

  [[nodiscard]] static skin::ScreenSchema schema();

  // Call every frame with what should show, nullptr for nothing. A different text
  // starts a new fade in.
  void paint(Ui& ui, Painter& p, const Spec* want);
  // True until the next frame after the hover moved, since only a frame can tell what the new
  // hover shows; the pack's fade asks for its own frames.
  [[nodiscard]] bool animating(const Ui& ui) const;
  [[nodiscard]] bool visible() const { return shown_; }
  [[nodiscard]] const std::string& text() const { return spec_.text; }

 private:
  Spec spec_;
  bool shown_ = false;
  unsigned fades_in_ = 0;
  const Surface* seen_owner_ = nullptr;
  int seen_id_ = 0;
};

}  // namespace crpp::ui
