#pragma once
// What the settings tabs draw alike (.heading, .toggle-row, .tip and so on), and the
// value that runs their CSS transitions. Tabs give the top of a row as upstream's
// style="top:..." inside .settings-inner, so it can be compared with main.js.
#include <string_view>

#include "crpp/ui/controls.hpp"

namespace crpp::ui {

inline constexpr Rect settings_view{296, 137, 566, 504};  // .settings-content
inline constexpr float row_top = 76;                      // ROW_TOP
inline constexpr float row_step = 44;                     // ROW_STEP
inline constexpr float settings_bottom_pad = 25;          // SETTINGS_BOTTOM_PAD
inline constexpr float dropdown_x = 285, dropdown_w = 246, dropdown_h = 38;  // .dropdown

// A CSS transition between off (0) and on (1). set() is called while painting, and
// the first call only takes the state, as an element's first style does.
class Fade {
 public:
  void set(const Ui& ui, bool on, float ms_on, float ms_off);
  void set(const Ui& ui, bool on, float ms) { set(ui, on, ms, ms); }
  [[nodiscard]] float value(const Ui& ui) const;
  [[nodiscard]] bool running(const Ui& ui) const;
  [[nodiscard]] bool on() const { return on_; }

 private:
  bool on_ = false, started_ = false;
  float from_ = 0, ms_ = 1;
  unsigned long long start_ = 0;
};

// .toggle-row input: the knob and track (.16s), the hover shade (.15s).
struct ToggleAnim {
  Fade knob, hover;
  [[nodiscard]] bool running(const Ui& ui) const { return knob.running(ui) || hover.running(ui); }
};

void paint_heading(Painter& p, const Theme& t, Rect area, float top, std::string_view text);
void paint_subheading(Painter& p, const Theme& t, Rect area, float top, std::string_view text);
// .volume-label beside a dropdown.
void paint_row_label(Painter& p, const Theme& t, Rect area, float top, std::string_view text);
// A .toggle-row; only the switch is clickable. `hint` is its small second part
// (.toggle-label small), with its line breaks written in, as DirectWrite has no
// word-break: keep-all.
void paint_toggle(Ui& ui, Surface* owner, Painter& p, Rect area, float top, std::string_view label, bool on, int id,
                  ToggleAnim& anim, std::string_view hint = {});
// .open-logs and .reset-settings: filled, grows a little under the pointer.
void paint_text_button(Ui& ui, Surface* owner, Painter& p, Rect r, std::string_view label, int id, Fade& hover);

[[nodiscard]] float text_width(const Painter& p, std::string_view text, const TextStyle& style);
// A .tip-like bubble filling `box`; alpha is how far it has faded in.
void paint_bubble(Painter& p, std::string_view text, Rect box, const TextStyle& style, float radius, Color fill, Color ink,
                  float alpha);

// Which way the list of a dropdown just opened goes, as main.js decides it. `button`
// is in stage coordinates.
void place_dropdown(Dropdown& dropdown, Rect button);

}  // namespace crpp::ui
