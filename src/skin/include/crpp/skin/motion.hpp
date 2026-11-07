#pragma once
// A value running between 0 and 1 as a state goes off and on, as a CSS transition does. A pack
// says the duration and the curve; what is under way is kept here, by a key the walk works out.
#include <cstdint>
#include <vector>

namespace crpp::skin {

enum class Curve : std::uint8_t { Ease, Linear };

// How far along the curve, for t from 0 to 1.
[[nodiscard]] float curve_at(Curve curve, float t);
// CSS's cubic-bezier(x1, y1, x2, y2) at t, whose y may overshoot.
[[nodiscard]] float bezier_at(float x1, float y1, float x2, float y2, float t);

struct TransitionSpec {
  bool on = false;
  float ms_on = 0, ms_off = 0;
  // What is on when first seen is already there, as an element's first style is; otherwise it
  // runs up from off.
  bool first_jumps = false;
  Curve curve = Curve::Ease;
  // A new value of this starts it anew, as a new element would: on, it runs up from off; off, it is off at once.
  float again = 0;
};

class Transitions {
 public:
  // Where it is now. `moving` says whether it still has to change. Without `animate` it is at its end.
  [[nodiscard]] float value(std::uint64_t key, const TransitionSpec& spec, unsigned long long now, bool animate, bool& moving);
  // A frame begins: what the last frame did not ask for is gone, as an element that left the page.
  void next_frame();
  [[nodiscard]] std::size_t size() const { return items_.size(); }

 private:
  struct Item {
    std::uint64_t key = 0;
    unsigned long long start = 0;
    float from = 0, span_ms = 0, again = 0;
    std::uint32_t frame = 0;
    bool on = false;
  };
  std::vector<Item> items_;  // by key
  std::uint32_t frame_ = 0;
};

}  // namespace crpp::skin
