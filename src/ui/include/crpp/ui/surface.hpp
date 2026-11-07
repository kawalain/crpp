#pragma once
// What every part of the screen is built on. A Surface draws itself, registers
// hit rectangles under ids of its own, and handles what lands on them. No surface
// knows another's ids, so a screen can grow without touching the others.
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "crpp/ui/gfx.hpp"
#include "crpp/ui/skin.hpp"
#include "crpp/ui/svg.hpp"
#include "crpp/ui/textfield.hpp"

namespace crpp::ui {

inline constexpr float stage_w = 951;
inline constexpr float stage_h = 692;

enum class Cursor : std::uint8_t { Arrow, Hand, IBeam };

// Milliseconds on the clock all motion runs on. The system's tick moves in steps of
// about 16 ms, too coarse for what moves every frame.
[[nodiscard]] unsigned long long clock_ms() noexcept;
// Stops the clock at this time, for rendering a moment of a motion; 0 restarts it.
void hold_clock_at(unsigned long long ms) noexcept;
// Seconds since 1970, as std::time gives them, unless held: --shot stops it so that the
// same scene always renders the same image.
[[nodiscard]] long long wall_clock_seconds() noexcept;
void hold_wall_clock(long long seconds) noexcept;

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
  std::vector<std::string> emoji_ids;  // what the picker offers, when it can show them
  Theme theme = light_cloud_palette(rgb(0x66ccff));

  [[nodiscard]] const Skin& skin() const { return skin_of(theme); }
  [[nodiscard]] ID2D1PathGeometry* cloud() const { return gfx.path(cloud_path_data); }

  // Never null once App exists.
  Prefs* prefs = nullptr;
  Dialogs* dialogs = nullptr;
  Toasts* toasts = nullptr;
  EmojiPicker* emoji_picker = nullptr;

  // Off for --shot, tests and a system set to show no animations: every transition
  // shows its end state.
  bool animations = true;
  // Set while painting by whatever needs another frame after this one.
  bool more_frames = false;

  // The stage is scaled to cover the window; the overflow is cropped, centered.
  float scale = 1, ox = 0, oy = 0, width = stage_w, height = stage_h;
  unsigned long long now_ms = 0;

  // The field that takes typing, and who decides what Enter does in it.
  TextField* focus = nullptr;
  Surface* focus_owner = nullptr;
  bool caret_on = true;
  Rect caret;  // window coordinates, for IME placement

  // Off while drawing what is on its way out: only what stays takes clicks.
  bool hits_enabled = true;

  // A surface that set this in click() gets drag() until the button goes up.
  Surface* capture = nullptr;

  // Where a screen and a modal go: the whole window, or what a Skin's own frame leaves.
  Rect content{0, 0, stage_w, stage_h};
  // What a modal takes clicks from: everything but a caption the system or its buttons need.
  Rect modal_area{0, 0, stage_w, stage_h};
  // Where a settings list has room to open.
  Rect dropdown_view{296, 137, 566, 504};

  // What the button went down on, for a Skin that shows it.
  Surface* pressed_owner = nullptr;
  int pressed_id = 0;
  [[nodiscard]] bool pressed(const Surface* owner, int id) const { return pressed_owner == owner && pressed_id == id; }

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

  // The opacity mask of an edge fade. They stay for reuse, since a scrolled list keeps
  // asking for the same one, until the render target they belong to goes away.
  [[nodiscard]] ID2D1LinearGradientBrush* edge_fade_mask(Painter& p, Rect area, float top, float bottom);
  void forget_render_target();

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
  struct FadeMask {
    int key;
    ComPtr<ID2D1LinearGradientBrush> brush;
  };
  std::vector<FadeMask> fade_masks_;
  ID2D1RenderTarget* masks_target_ = nullptr;
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
  // Is this a link? A Skin whose buttons keep the arrow still shows the hand on one.
  [[nodiscard]] virtual bool is_link(int /*id*/) const { return false; }

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
