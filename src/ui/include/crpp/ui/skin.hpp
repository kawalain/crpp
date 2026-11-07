#pragma once
// What a Skin draws for the surfaces all Skins share. State, hit ids and layout stay with the
// surface; here is only the shape of its parts. See docs/adr/0005-skins-own-shape-and-layout.md.
#include <string_view>

#include "crpp/ui/gfx.hpp"

namespace crpp::ui {

// Who a raised panel belongs to, since each Skin groups them in its own way.
enum class PanelKind : std::uint8_t { Modal, List, Update, Notice, Toast, Menu, Popup, Picker };
enum class ButtonKind : std::uint8_t { Modal, Notice, Update, Toast, Settings, Account };

struct ButtonLook {
  ButtonKind kind = ButtonKind::Modal;
  bool primary = true;
  bool hot = false;
  bool pressed = false;
  float shown = 1;      // the surface's own fade
  float hover_at = -1;  // how far a hover has faded in, for a kind that fades; below 0: `hot` says
};

// How a dropdown's list is measured.
struct ListMetrics {
  float pad, item_h, gap, max_h, margin;
};

class Skin {
 public:
  virtual ~Skin() = default;

  // Does the window show the scaled stage, or does everything keep its own size?
  [[nodiscard]] virtual bool scales_stage() const = 0;
  // Screen transitions, wheel scrolling that settles, stretch at the ends, edge fades.
  [[nodiscard]] virtual bool moves() const = 0;

  virtual void panel(Painter& p, const Theme& t, Rect r, PanelKind kind, float shown = 1) const = 0;
  // What dims everything under a modal; shown is its transition's progress.
  virtual void backdrop(Painter& p, const Theme& t, Rect window, float shown) const = 0;
  virtual void button(Painter& p, const Theme& t, Rect r, std::string_view label, const ButtonLook& look) const = 0;
  // `at` runs from 0 at the off end to 1 at the on end.
  virtual void switch_at(Painter& p, const Theme& t, Rect r, bool on, float at, bool disabled, bool hot) const = 0;
  // r is the hit area; `at` is where the thumb stands, from 0 to 1, and its middle stands at
  // r.x + slider_thumb / 2 + (r.w - slider_thumb) * at.
  virtual void slider(Painter& p, const Theme& t, Rect r, float at) const = 0;
  virtual void dropdown_button(Painter& p, const Theme& t, Rect r, std::string_view text, bool open, bool hot, bool disabled) const = 0;
  [[nodiscard]] virtual ListMetrics list_metrics() const = 0;
  virtual void list_item(Painter& p, const Theme& t, Rect r, std::string_view text, bool selected, bool hot) const = 0;
  // `hover` runs from 0 to 1, and `shown` is how far it has faded in.
  virtual void scrollbar(Painter& p, const Theme& t, Rect thumb, float hover, float shown) const = 0;
  // A small label's box. Returns the colour to write in it.
  [[nodiscard]] virtual Color bubble(Painter& p, const Theme& t, Rect r, float radius, Color fill, Color ink, float alpha) const = 0;
  // One choice in a grid, as the emoji picker's.
  virtual void cell(Painter& p, const Theme& t, Rect r, bool selected, bool hot) const = 0;
  // A line of a pop-up menu, with its text.
  virtual void menu_item(Painter& p, const Theme& t, Rect r, std::string_view text, bool active, bool hot, bool disabled) const = 0;
  // What a profile's icon stands on; grow is how much a hover enlarges it.
  virtual void avatar(Painter& p, const Theme& t, Rect r, float grow, bool lit, float shown) const = 0;
  virtual void progress(Painter& p, const Theme& t, Rect track, int percent) const = 0;
  virtual void close_button(Painter& p, const Theme& t, Rect r, bool hot, float shown) const = 0;
};

// inline constexpr so that code laying out a slider's tip can say where the thumb is.
inline constexpr float slider_thumb = 21;

[[nodiscard]] const Skin& skin_of(SkinId id);
[[nodiscard]] inline const Skin& skin_of(const Theme& t) { return skin_of(t.skin); }

}  // namespace crpp::ui
