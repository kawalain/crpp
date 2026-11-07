#include "crpp/skin/motion.hpp"

#include <algorithm>

namespace crpp::skin {

float curve_at(Curve curve, float t) {
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  if (curve == Curve::Linear) return t;
  // cubic-bezier(.25, .1, .25, 1): find the curve's parameter for x = t, then its y.
  const auto bezier = [](float a, float b, float u) {
    const float v = 1 - u;
    return 3 * v * v * u * a + 3 * v * u * u * b + u * u * u;
  };
  float low = 0, high = 1;
  for (int i = 0; i < 12; ++i) {
    const float u = (low + high) / 2;
    if (bezier(0.25f, 0.25f, u) < t) low = u;
    else high = u;
  }
  return bezier(0.1f, 1.0f, (low + high) / 2);
}

namespace {

float at(const auto& item, Curve curve, unsigned long long now) {
  const float to = item.on ? 1.0f : 0.0f;
  if (item.span_ms <= 0) return to;
  const float elapsed = now > item.start ? static_cast<float>(now - item.start) : 0.0f;
  return item.from + (to - item.from) * curve_at(curve, elapsed / item.span_ms);
}

}  // namespace

float Transitions::value(std::uint64_t key, const TransitionSpec& spec, unsigned long long now, bool animate, bool& moving) {
  moving = false;
  const auto found = std::ranges::lower_bound(items_, key, {}, &Item::key);
  const bool known = found != items_.end() && found->key == key;
  if (!animate) {
    if (known) items_.erase(found);
    return spec.on ? 1.0f : 0.0f;
  }
  if (!known) {
    // Off and never seen on: nothing to keep, unless it must tell a change from a first sight.
    if (!spec.on && !spec.first_jumps) return 0;
    Item item{.key = key, .again = spec.again, .frame = frame_, .on = spec.on};
    if (spec.on && !spec.first_jumps) {
      item.start = now;
      item.span_ms = spec.ms_on;
    }
    const auto placed = items_.insert(found, item);
    moving = placed->span_ms > 0;
    return at(*placed, spec.curve, now);
  }
  Item& item = *found;
  item.frame = frame_;
  if (item.on != spec.on) {
    item.from = at(item, spec.curve, now);
    item.on = spec.on;
    item.start = now;
    // Turning around midway takes proportionally less time.
    item.span_ms = spec.on ? spec.ms_on * (1 - item.from) : spec.ms_off * item.from;
  }
  if (item.again != spec.again) {
    item.again = spec.again;
    item.from = 0;
    item.start = now;
    item.span_ms = spec.on ? spec.ms_on : 0;
  }
  const float shown = at(item, spec.curve, now);
  moving = item.span_ms > 0 && static_cast<float>(now - item.start) < item.span_ms;
  if (!moving) {
    item.span_ms = 0;
    if (!spec.on && !spec.first_jumps) items_.erase(found);
  }
  return shown;
}

void Transitions::next_frame() {
  std::erase_if(items_, [&](const Item& item) { return item.frame != frame_; });
  ++frame_;
}

}  // namespace crpp::skin
