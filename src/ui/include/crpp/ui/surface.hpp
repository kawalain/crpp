#pragma once
// What every part of the screen is built on. A Surface draws itself, registers
// hit rectangles under ids of its own, and handles what lands on them. No surface
// knows another's ids, so a screen can grow without touching the others.
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "crpp/skin/draw.hpp"
#include "crpp/skin/motion.hpp"
#include "crpp/skin/pack.hpp"
#include "crpp/ui/clipboard.hpp"
#include "crpp/ui/gfx.hpp"
#include "crpp/ui/skin_choice.hpp"
#include "crpp/ui/svg.hpp"
#include "crpp/ui/textfield.hpp"

namespace crpp::skin {
struct Pack;
struct Screen;
}  // namespace crpp::skin

namespace crpp::ui {

inline constexpr float stage_w = 951;
inline constexpr float stage_h = 692;

enum class Cursor : std::uint8_t { Arrow, Hand, IBeam };

// What the context menu offers, in the order it shows them.
enum class MenuCommand : std::uint8_t { CopyLink, Cut, Copy, Paste, Delete, SelectAll };
struct MenuItem {
  MenuCommand command;
  bool enabled = true;  // shown greyed when not
};

// What a stop of the keyboard is, which decides the keys it takes. The same in every Skin.
enum class Role : std::uint8_t { Button, Toggle, Slider, Dropdown, Tab, List, Field, Link };

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

// The numbers of the motion that stays in the engine: a pack that moves gives them all. These
// are for what the program draws without a pack.
struct EngineMotion {
  float scroll_settle_ms = 32, scroll_return_ms = 52, scroll_stretch = 48, edge_fade = 27;
  float screen_fade_ms = 180, screen_rise = 8;
  float speaking_hold_ms = 160, speaking_fade_ms = 100;
  float hold_wait_ms = 400, hold_repeat_ms = 50;
};

class Surface;
class Prefs;
class Dialogs;
class Toasts;
class EmojiPicker;

// What each window of the app has of its own. The surfaces read it from Ui, which holds the
// state of the window being painted or handling input; App swaps the others in and out.
struct WindowState {
  // Which window this is: 0 the main one, otherwise a panel with a window of its own.
  int window = 0;

  // Set while painting by whatever needs another frame after this one.
  bool more_frames = false;
  // What a Skin Pack's transitions have under way in this window.
  skin::Transitions transitions;

  // The stage is scaled to cover the window; the overflow is cropped, centered.
  float scale = 1, ox = 0, oy = 0, width = stage_w, height = stage_h;

  // The field that takes typing, and who decides what Enter does in it.
  TextField* focus = nullptr;
  Surface* focus_owner = nullptr;
  // Whose selection of text that is only read takes the keys while no field does.
  Surface* selection_owner = nullptr;
  bool caret_on = true;
  Rect caret;  // window coordinates, for IME placement

  // The control the keyboard is on, a hit of a surface with a Role, and whether that shows: Tab, an
  // arrow or Alt shows it, the mouse hides it, as WM_UPDATEUISTATE does in a dialog.
  Surface* stop_owner = nullptr;
  int stop_id = 0;
  bool cues = false;
  // The underlines of access keys, which only Alt shows, as UISF_HIDEACCEL apart from the focus.
  bool access_cues = false;
  // Whose backdrop holds the keys now, and where the keyboard was under each layer that opened since:
  // it goes back there when the layer closes.
  const Surface* keys_holder = nullptr;
  struct HeldStop {
    const Surface* holder;
    Surface* owner;
    int id;
  };
  std::vector<HeldStop> held_stops;

  // A surface that set this in click(), on a hit that acts on the press, gets drag() until the button goes up.
  Surface* capture = nullptr;

  // Where a screen and a modal go: the whole window, or what a Skin's own frame leaves.
  Rect content{0, 0, stage_w, stage_h};
  // What a modal takes clicks from: everything but a caption the system or its buttons need.
  Rect modal_area{0, 0, stage_w, stage_h};
  // Where a settings list has room to open.
  Rect dropdown_view{296, 137, 566, 504};

  // What the button went down on, and whether it acts when the button comes up there.
  Surface* pressed_owner = nullptr;
  int pressed_id = 0;
  bool pressed_waits = false;
  // Where the pointer is while it is held, and whether holding acts again and again.
  float pressed_x = 0, pressed_y = 0;
  bool pressed_repeats = false;

  Surface* hover_owner = nullptr;
  int hover_id = 0;

  struct Hit {
    Rect rect;  // window coordinates
    Surface* owner;
    int id;
    // Only this shape takes it: path data, its origin on the stage.
    std::string_view shape = {};
    float shape_x = 0, shape_y = 0;
    // For the keyboard: where it was read, as the pack reads; what the pack put first; whether it acts
    // now; and whether a clip left any of it to click. A hidden one is still a stop.
    std::uint32_t number = 0;
    float tab_order = 0;
    bool live = true, shown = true;
    Rect whole;  // before any clip, to scroll it into view
  };

 protected:
  std::vector<Hit> hits_;
  std::vector<Rect> hit_clips_;
  std::uint32_t hits_read_ = 0;
};

// What the surfaces share: drawing resources, the geometry of the current frame,
// the hit list, the keyboard focus and the app-wide services.
class Ui : public WindowState {
 public:
  // Puts this window's state in `other` and takes that one's.
  void swap_window(WindowState& other) { std::swap(static_cast<WindowState&>(*this), other); }

  Gfx gfx;
  std::optional<EmojiCache> emoji;
  std::vector<std::string> emoji_ids;  // what the picker offers, when it can show them
  Theme theme;

  // Does the window show the stage scaled to it, or does everything keep its own size in a frame
  // the app draws? Without a pack, the first.
  [[nodiscard]] bool scales_stage() const;
  // Screen transitions, wheel scrolling that settles, stretch at the ends, edge fades.
  [[nodiscard]] bool moves() const;

  // The Skin Pack in use and this Theme's Palette in it, while data draws what it has.
  const skin::Pack* pack = nullptr;
  EngineMotion motion;
  // Every pack there is to choose from.
  std::vector<SkinInfo> skins;
  // The packs a Repository offers that are not installed, to fetch when one of their Themes is picked.
  std::vector<SkinInfo> offered;
  ThemeFetch theme_fetch;
  int palette = -1;
  // This screen in the pack, if the pack is for this Theme and has it.
  [[nodiscard]] const skin::Screen* pack_screen(std::string_view name) const;
  // Where a screen is drawn: on the stage when that is scaled to the window, otherwise inside the frame.
  [[nodiscard]] Rect screen_area() const;


  // Never null once App exists.
  Prefs* prefs = nullptr;
  Clipboard* clipboard = nullptr;
  Dialogs* dialogs = nullptr;
  Toasts* toasts = nullptr;
  EmojiPicker* emoji_picker = nullptr;

  // The system's "Underline access keys": the keyboard's place always shows.
  bool always_cues = false;

  // Off for --shot, tests and a system set to show no animations: every transition
  // shows its end state.
  bool animations = true;
  unsigned long long now_ms = 0;

  // Off while drawing what is on its way out: only what stays takes clicks.
  bool hits_enabled = true;

  // How many presses in a row the last one was, 1 to 3, by the system's double-click time and
  // size, which the shell sets.
  int clicks = 1;
  unsigned double_click_ms = 500;
  float double_click_size = 4;  // DIPs either way

  // What a field does with a key: Ctrl+C, X and V go through the clipboard, the rest to the field.
  bool edit_key(TextField& field, unsigned vk, bool ctrl, bool shift);
  // A field's context menu, and what its items do.
  [[nodiscard]] std::vector<MenuItem> field_menu(const TextField& field);
  void field_command(TextField& field, MenuCommand command);
  // Puts text on the clipboard; a failure says so in a toast.
  bool copy(std::wstring_view text);
  void paste_into(TextField& field);

  // It looks pressed while the pointer is on it, or while it is being dragged.
  [[nodiscard]] bool pressed(const Surface* owner, int id) const { return pressed_owner == owner && pressed_id == id && (capture || hot(owner, id)); }

  void begin_frame(float window_w, float window_h, unsigned long long now);
  void set_focus(Surface* owner, TextField* field) {
    focus_owner = owner;
    focus = field;
  }

  // Without a place, a hit is read where it is added and acts.
  void add_hit(Surface* owner, Rect stage_rect, int id, const skin::HitPlace* place = nullptr);
  void add_hit_window(Surface* owner, Rect window_rect, int id, const skin::HitPlace* place = nullptr);
  void add_hit_shape(Surface* owner, Rect stage_rect, int id, std::string_view data, float x, float y, const skin::HitPlace* place = nullptr);
  // The number of the next hit read, for one whose children are added before it.
  [[nodiscard]] std::uint32_t next_hit() { return hits_read_++; }
  // Hits added until pop_hit_clip are cut to this rectangle, for scrolled content.
  void push_hit_clip(Rect window_rect);
  void pop_hit_clip();

  [[nodiscard]] bool hot(const Surface* owner, int id) const { return hover_owner == owner && hover_id == id; }
  // The keyboard is on it, and that shows.
  [[nodiscard]] bool focused(const Surface* owner, int id) const;
  void set_stop(Surface* owner, int id) {
    stop_owner = owner;
    stop_id = id;
  }
  // The stops Tab goes through in this window, in order: above the last hit that holds the keys.
  [[nodiscard]] std::vector<const Hit*> tab_stops() const;
  // The stop the keyboard is on, if it is one of those now.
  [[nodiscard]] const Hit* current_stop() const;
  // The backdrop of the top layer, read last of those that hold the keys; none when no layer is open.
  [[nodiscard]] const Hit* holding_hit() const;
  // The keyboard goes to this stop of the same surface, and that shows, as an arrow moves it in a list.
  void move_stop_to(Surface* owner, int id) {
    set_stop(owner, id);
    cues = true;
  }
  [[nodiscard]] float to_stage_x(float x) const { return (x - ox) / scale; }
  [[nodiscard]] float to_stage_y(float y) const { return (y - oy) / scale; }
  [[nodiscard]] Rect to_window(Rect r) const { return {ox + r.x * scale, oy + r.y * scale, r.w * scale, r.h * scale}; }

  // The opacity mask of an edge fade. They stay for reuse, since a scrolled list keeps
  // asking for the same one, until the render target they belong to goes away.
  [[nodiscard]] ID2D1LinearGradientBrush* edge_fade_mask(Painter& p, Rect area, float top, float bottom);
  void forget_render_target();

  // The emoji with this Twemoji id, or the default droplet for an empty or unknown one.
  [[nodiscard]] const SvgImage& emoji_image(std::string_view id);

  [[nodiscard]] const Hit* hit_at(float x, float y) const;
  // Where a hit was last drawn and can be clicked, for tests driving the UI without a window.
  [[nodiscard]] const Hit* find_hit(const Surface* owner, int id) const;
  // The same, though a clip may hide it.
  [[nodiscard]] const Hit* find_stop(const Surface* owner, int id) const;

 private:
  struct FadeMask {
    int key;
    ComPtr<ID2D1LinearGradientBrush> brush;
  };
  std::vector<FadeMask> fade_masks_;
  ID2D1RenderTarget* masks_target_ = nullptr;
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
  // When the button comes up on the hit it went down on, or at once where acts_on_press says so.
  virtual void click(int /*id*/, float /*x*/, float /*y*/, bool /*shift*/) {}
  // True for what cannot wait for the button to come up: what is dragged or scrolled by
  // holding it, a text field, and what opens a list.
  [[nodiscard]] virtual bool acts_on_press(int /*id*/) const { return false; }
  // True for a hit that acts on the press and again while it is held and the pointer stays on it.
  [[nodiscard]] virtual bool repeats(int /*id*/) const { return false; }
  virtual bool right_click(int /*id*/, float /*x*/, float /*y*/) { return false; }
  virtual void drag(float /*x*/, float /*y*/) {}
  virtual void release() {}
  // Another click went elsewhere: a selection of text that is only read goes.
  virtual void drop_selection() {}
  // What the context menu offers for a right-click on this hit, at x, y in the window. Empty:
  // none opens. id -1 is the menu key or Shift+F10, for what has the keys.
  [[nodiscard]] virtual std::vector<MenuItem> context_menu(int /*id*/, float /*x*/, float /*y*/) { return {}; }
  // An item of that menu was chosen.
  virtual void menu_command(MenuCommand /*command*/) {}
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

  // The keyboard: what a hit is to it, if it is a stop at all.
  [[nodiscard]] virtual std::optional<Role> role(int /*id*/) const { return std::nullopt; }
  // The stop a hit belongs to. A list, a grid or a row of tabs is one stop, wherever in it the keyboard
  // or a click is: the item the keyboard is on, or the whole list's own hit. Tab stops only there.
  [[nodiscard]] virtual int stop_for(int id) const { return id; }
  // A hit over what is under it, as a backdrop: Tab stays among what is read after it.
  [[nodiscard]] virtual bool holds_keys(int /*id*/) const { return false; }
  // The keyboard came to this stop. A field takes the keys here.
  virtual void focus(int /*id*/) {}
  // A key while the keyboard is on this stop and no field has the keys: arrows and the like. True if taken.
  virtual bool stop_key(int /*id*/, unsigned /*vk*/, bool /*ctrl*/, bool /*shift*/, bool /*alt*/) { return false; }
  // A key the whole layer takes while it is open, wherever the keyboard is in it, as Ctrl+Tab in a
  // panel of tabs. Offered top layer first.
  virtual bool layer_key(unsigned /*vk*/, bool /*ctrl*/, bool /*shift*/) { return false; }
  // Where the keyboard goes when this layer opens: one of its stops, or the first when it says none.
  [[nodiscard]] virtual std::optional<int> opening_stop() const { return std::nullopt; }
  // A stop at this place in the window, scrolled out of view, came to have the keyboard: true if this
  // layer scrolled to show it. Offered top layer first.
  virtual bool reveal(Rect /*whole*/) { return false; }

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
