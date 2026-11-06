#pragma once
// A single-line text input drawn with DirectWrite, with caret, selection and
// IME composition (Korean input). Owns only text state; the window forwards
// keyboard, IME and mouse events to it.
#include <string>
#include <string_view>

#include "crpp/ui/gfx.hpp"

namespace crpp::ui {

class TextField {
 public:
  std::size_t max_length = 0;  // 0 = unlimited (UTF-16 units)
  bool uppercase = false;
  bool lowercase = false;
  bool (*accept)(wchar_t) = nullptr;  // when set, what it refuses cannot be typed
  bool password = false;              // shown masked, and cannot be copied while it is
  bool multiline = false;  // allows line breaks (Shift+Enter); the owner decides what Enter does

  [[nodiscard]] const std::wstring& text() const { return text_; }
  void set_text(std::wstring_view s);
  void clear() { set_text({}); }

  // Typed or pasted text. Control characters are dropped.
  void insert(std::wstring_view s);
  void set_composition(std::wstring_view s) { composition_ = s; }
  [[nodiscard]] bool composing() const { return !composition_.empty(); }

  // Editing keys. Returns true if the key was used.
  bool key_down(unsigned vk, bool ctrl, bool shift);

  void select_all();
  // Height of the laid-out text when wrapped to width.
  [[nodiscard]] float measure_height(const Gfx& gfx, const TextStyle& style, float width) const;
  // Moves the caret one wrapped line up (dir < 0) or down.
  void move_vertical(const Gfx& gfx, const TextStyle& style, float width, int dir, bool extend);
  void click_at(const Gfx& gfx, const TextStyle& style, float x_in_text, bool extend);

  // Draws inside box (the text origin is the box's left edge; the line box is
  // box.h tall). Draws the placeholder when empty and not composing.
  // Returns the caret rectangle in the same coordinates, for IME placement.
  Rect paint(Painter& p, Rect box, const TextStyle& style, Color text, std::wstring_view placeholder,
             const TextStyle& placeholder_style, Color placeholder_color, Color caret, Color selection, bool focused,
             bool caret_on) const;

 private:
  [[nodiscard]] std::wstring shown() const;
  void erase_selection();
  [[nodiscard]] bool has_selection() const { return anchor_ != caret_; }
  void move_to(std::size_t pos, bool extend);

  std::wstring text_;
  std::wstring composition_;
  std::size_t caret_ = 0;
  std::size_t anchor_ = 0;
};

}  // namespace crpp::ui
