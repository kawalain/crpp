#pragma once
// What every part of the screen is built on. A Surface draws itself, registers
// hit rectangles under ids of its own, and handles what lands on them. No surface
// knows another's ids, so a screen can grow without touching the others.
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "crpp/ui/gfx.hpp"
#include "crpp/ui/svg.hpp"
#include "crpp/ui/textfield.hpp"

namespace crpp::ui {

inline constexpr float stage_w = 951;
inline constexpr float stage_h = 692;

enum class Cursor : std::uint8_t { Arrow, Hand, IBeam };

inline D2D1_MATRIX_3X2_F scale_about(float cx, float cy, float s) {
  return D2D1::Matrix3x2F::Translation(-cx, -cy) * D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(cx, cy);
}

class Surface;
class Prefs;
class Dialogs;
class Toasts;
class EmojiPicker;

// What the surfaces share: drawing resources, the geometry of the current frame,
// the hit list, the keyboard focus and the app-wide services.
class Ui {
 public:
  Gfx gfx;
  std::optional<EmojiCache> emoji;
  Theme theme = make_theme(ThemeId::Cloudroom);
  ComPtr<ID2D1PathGeometry> cloud;

  // Never null once App exists.
  Prefs* prefs = nullptr;
  Dialogs* dialogs = nullptr;
  Toasts* toasts = nullptr;
  EmojiPicker* emoji_picker = nullptr;

  // Off for --shot and tests: every transition shows its end state.
  bool animations = true;

  // The stage is scaled to cover the window; the overflow is cropped, centered.
  float scale = 1, ox = 0, oy = 0, width = stage_w, height = stage_h;
  unsigned long long now_ms = 0;

  // The field that takes typing, and who decides what Enter does in it.
  TextField* focus = nullptr;
  Surface* focus_owner = nullptr;
  bool caret_on = true;
  Rect caret;  // window coordinates, for IME placement

  // A surface that set this in click() gets drag() until the button goes up.
  Surface* capture = nullptr;

  void begin_frame(float window_w, float window_h, unsigned long long now);
  void set_focus(Surface* owner, TextField* field) {
    focus_owner = owner;
    focus = field;
  }

  void add_hit(Surface* owner, Rect stage_rect, int id);
  void add_hit_window(Surface* owner, Rect window_rect, int id);
  // Hits added until pop_hit_clip are cut to this rectangle, for scrolled content.
  void push_hit_clip(Rect window_rect);
  void pop_hit_clip();

  [[nodiscard]] bool hot(const Surface* owner, int id) const { return hover_owner == owner && hover_id == id; }
  [[nodiscard]] float to_stage_x(float x) const { return (x - ox) / scale; }
  [[nodiscard]] float to_stage_y(float y) const { return (y - oy) / scale; }
  [[nodiscard]] Rect to_window(Rect r) const { return {ox + r.x * scale, oy + r.y * scale, r.w * scale, r.h * scale}; }

  // The emoji with this Twemoji id, or the default droplet for an empty or unknown one.
  [[nodiscard]] const SvgImage& emoji_image(std::string_view id);

  struct Hit {
    Rect rect;  // window coordinates
    Surface* owner;
    int id;
  };
  [[nodiscard]] const Hit* hit_at(float x, float y) const;
  // Where a hit was last drawn, for tests driving the UI without a window.
  [[nodiscard]] const Hit* find_hit(const Surface* owner, int id) const;

  Surface* hover_owner = nullptr;
  int hover_id = 0;

 private:
  std::vector<Hit> hits_;
  std::vector<Rect> hit_clips_;
};

class Surface {
 public:
  explicit Surface(Ui& ui) : ui_(ui) {}
  virtual ~Surface() = default;
  Surface(const Surface&) = delete;
  Surface& operator=(const Surface&) = delete;

  // A hit whose real shape is not its rectangle can refuse the point.
  [[nodiscard]] virtual bool accepts(int /*id*/, float /*x*/, float /*y*/) const { return true; }
  // Before any click is dispatched, to every surface on screen; owner is null when
  // nothing was hit. Popups close here when the click is outside them.
  virtual void before_click(const Surface* /*owner*/, int /*id*/) {}
  virtual void click(int /*id*/, float /*x*/, float /*y*/, bool /*shift*/) {}
  virtual bool right_click(int /*id*/, float /*x*/, float /*y*/) { return false; }
  virtual void drag(float /*x*/, float /*y*/) {}
  virtual void release() {}
  // Offered top surface first; true stops it there.
  virtual bool wheel(float /*x*/, float /*y*/, float /*delta*/) { return false; }
  // Escape, offered top surface first; true means it closed something.
  virtual bool escape() { return false; }
  // Enter in a field this surface focused.
  virtual void enter(bool /*shift*/) {}
  // A key in a field this surface focused. The default edits the field.
  virtual bool key(unsigned vk, bool ctrl, bool shift);
  [[nodiscard]] virtual Cursor cursor(int /*id*/) const { return Cursor::Hand; }

  // A running effect that needs frames.
  [[nodiscard]] virtual bool animating() const { return false; }
  // Expires timed effects; true if something changed.
  virtual bool tick(unsigned long long /*now_ms*/) { return false; }
  // When tick() is next needed though nothing is animating; 0 = never. It keeps a
  // waiting effect from needing frames: idle means no timers.
  [[nodiscard]] virtual unsigned long long wake_at() const { return 0; }

 protected:
  Ui& ui_;
};

}  // namespace crpp::ui
