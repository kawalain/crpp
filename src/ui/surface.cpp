#include "crpp/ui/surface.hpp"

#include <algorithm>

namespace crpp::ui {

namespace {
constexpr const char* default_emoji = "1f4a7";
}  // namespace

void Ui::begin_frame(float window_w, float window_h, unsigned long long now) {
  width = window_w;
  height = window_h;
  now_ms = now;
  scale = std::max(window_w / stage_w, window_h / stage_h);
  ox = (window_w - stage_w * scale) / 2;
  oy = (window_h - stage_h * scale) / 2;
  hits_.clear();
  hit_clips_.clear();
}

void Ui::add_hit(Surface* owner, Rect r, int id) { add_hit_window(owner, to_window(r), id); }

void Ui::add_hit_window(Surface* owner, Rect r, int id) {
  for (const Rect& clip : hit_clips_) {
    const float left = std::max(r.x, clip.x), top = std::max(r.y, clip.y);
    const float right = std::min(r.right(), clip.right()), bottom = std::min(r.bottom(), clip.bottom());
    if (right <= left || bottom <= top) return;
    r = {left, top, right - left, bottom - top};
  }
  hits_.push_back({r, owner, id});
}

void Ui::push_hit_clip(Rect window_rect) { hit_clips_.push_back(window_rect); }

void Ui::pop_hit_clip() { hit_clips_.pop_back(); }

const Ui::Hit* Ui::hit_at(float x, float y) const {
  // Later hits are drawn on top.
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
    if (it->rect.contains(x, y) && it->owner->accepts(it->id, x, y)) return &*it;
  }
  return nullptr;
}

namespace {
unsigned long long held_clock = 0;
}

unsigned long long clock_ms() noexcept {
  if (held_clock != 0) return held_clock;
  static const auto frequency = [] {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
  }();
  LARGE_INTEGER counter{};
  QueryPerformanceCounter(&counter);
  return static_cast<unsigned long long>(counter.QuadPart / frequency * 1000 + counter.QuadPart % frequency * 1000 / frequency);
}

void hold_clock_at(unsigned long long ms) noexcept { held_clock = ms; }

const Ui::Hit* Ui::find_hit(const Surface* owner, int id) const {
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
    if (it->owner == owner && it->id == id) return &*it;
  }
  return nullptr;
}

const SvgImage& Ui::emoji_image(std::string_view id) {
  const SvgImage* image = &emoji->get(gfx, id.empty() ? std::string(default_emoji) : std::string(id));
  if (image->empty()) image = &emoji->get(gfx, default_emoji);  // unknown emoji id
  return *image;
}

bool Surface::key(unsigned vk, bool ctrl, bool shift) { return ui_.focus && ui_.focus->key_down(vk, ctrl, shift); }

}  // namespace crpp::ui
