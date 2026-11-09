#pragma once
// A text input drawn with DirectWrite, with caret, selection and IME composition
// (Korean input). Owns only text state; the window forwards
// keyboard, IME and mouse events to it.
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "crpp/ui/gfx.hpp"

namespace crpp::ui {

class TextField {
 public:
  std::size_t max_length = 0;  // 0 = unlimited; counts characters (code points), as the server does
  bool uppercase = false;
  bool lowercase = false;
  bool (*accept)(wchar_t) = nullptr;  // when set, what it refuses cannot be typed
  bool password = false;              // shown masked, and cannot be copied while it is
  bool multiline = false;  // allows line breaks (Shift+Enter); the owner decides what Enter does
  bool compact = false;    // pasted spaces and line breaks are dropped, as in a code

  [[nodiscard]] const std::wstring& text() const { return text_; }
  // Characters (code points) in the text, the unit of max_length.
  [[nodiscard]] std::size_t length() const;
  void set_text(std::wstring_view s);
  void clear() { set_text({}); }

  // Typed text. Control characters are dropped.
  void insert(std::wstring_view s);
  // Pasted text: line breaks and tabs become a space on one line, a compact field drops every
  // space, and what goes past the maximum length is cut.
  void paste(std::wstring_view s);
  // Composing replaces a selection at once, if what is composed can go in this field.
  void set_composition(std::wstring_view s);
  [[nodiscard]] bool composing() const { return !composition_.empty(); }

  // Editing keys, Ctrl with an arrow, Backspace or Delete going by word. The clipboard's keys
  // are the owner's (Ui::edit_key). Returns true if the key was used.
  bool key_down(unsigned vk, bool ctrl, bool shift);

  void select_all();
  [[nodiscard]] std::size_t caret() const { return caret_; }
  // What a copy takes: the selection, or nothing for a password.
  [[nodiscard]] std::wstring selected_text() const;
  [[nodiscard]] bool can_copy() const { return !password && has_selection(); }
  [[nodiscard]] bool can_delete() const { return has_selection(); }
  // Whether pos is inside the selection, where a right-click leaves it as it is.
  [[nodiscard]] bool selects(std::size_t pos) const { return has_selection() && pos >= std::min(caret_, anchor_) && pos <= std::max(caret_, anchor_); }
  // A right-click there: outside the selection the caret moves to it, as in a browser.
  void right_press(std::size_t pos) {
    if (!selects(pos)) press(pos, 1, false);
  }
  // Removes the selection, as Delete does.
  void erase();

  // A press of the button at a place in the text: one click puts the caret there, two take the
  // word and three the paragraph; Shift extends what is selected. A drag that follows goes by
  // the same unit.
  void press(std::size_t pos, int clicks, bool extend);
  void drag_to(std::size_t pos);
  // The place in the text nearest a point, the text's origin at 0,0. wrap_width 0 is one line.
  [[nodiscard]] std::size_t position_at(const Gfx& gfx, const TextStyle& style, float x, float y, float wrap_width = 0) const;
  // Moves the caret one wrapped line up (dir < 0) or down.
  void move_vertical(const Gfx& gfx, const TextStyle& style, float width, int dir, bool extend);

  // Draws inside box (the text origin is the box's left edge; the line box is
  // box.h tall). Draws the placeholder when empty and not composing.
  // Returns the caret rectangle in the same coordinates, for IME placement.
  Rect paint(Painter& p, Rect box, const TextStyle& style, Color text, std::wstring_view placeholder,
             const TextStyle& placeholder_style, Color placeholder_color, Color caret, Color selection, bool focused,
             bool caret_on) const;

  // The text as drawn: dots for a password, with what is being composed.
  [[nodiscard]] std::wstring shown() const;

 private:
  void erase_selection();
  [[nodiscard]] bool has_selection() const { return anchor_ != caret_; }
  void move_to(std::size_t pos, bool extend);
  // The unit a press goes by around pos: [start, end).
  [[nodiscard]] std::pair<std::size_t, std::size_t> unit_at(std::size_t pos, int clicks) const;

  std::wstring text_;
  std::wstring composition_;
  std::size_t caret_ = 0;
  std::size_t anchor_ = 0;
  // What the last press took, which a drag keeps whole.
  int clicks_ = 1;
  std::size_t origin_lo_ = 0, origin_hi_ = 0;
};

}  // namespace crpp::ui
