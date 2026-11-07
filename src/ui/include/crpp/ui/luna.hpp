#pragma once
// The parts the Luna Skin is made of, for its own layouts: values in
// docs/research/retro-messenger-look.md. Everything is a fixed size in DIPs. Painter puts boxes
// and lines on whole pixels; a part of several lines starts from Painter::snap and counts in
// Painter::line, so that its lines meet at any scale.
#include <string_view>

#include "crpp/ui/surface.hpp"

namespace crpp::ui::luna {

inline constexpr float button_w = 75, button_h = 23, field_h = 23;
inline constexpr float check_size = 13, row_h = 20;
inline constexpr float caption_h = 28, frame_w = 3, menu_h = 20, window_radius = 8;
inline constexpr float scrollbar_w = 17;

// Body text at this language's size, centered in a line of the given height.
[[nodiscard]] TextStyle body(const Gfx& gfx, float line_height = 16, Align align = Align::Left);
[[nodiscard]] inline TextStyle body(const Painter& p, float line_height = 16, Align align = Align::Left) { return body(p.gfx(), line_height, align); }
[[nodiscard]] TextStyle bold(const Painter& p, float line_height = 16, Align align = Align::Left);
// Text that wraps, for a paragraph.
[[nodiscard]] TextStyle paragraph(const Painter& p, Align align = Align::Left);

struct ButtonState {
  bool hot = false, pressed = false, off = false, primary = false, focused = false;
};
void button(Painter& p, const Theme& t, Rect r, std::string_view label, const ButtonState& state);
// A button's width for this label: 75 at least, as old dialogs have them.
[[nodiscard]] float button_width(const Painter& p, std::string_view label);
// A flat button that rises under the pointer. `icon` draws its picture in a 16 px square.
void toolbar_button(Painter& p, const Theme& t, Rect r, std::string_view label, bool hot, bool pressed, bool lit);

// A white field with its line; the text goes inside.
void field(Painter& p, const Theme& t, Rect r, bool off = false);
// A sunken white area, for lists and the chat.
void sunken(Painter& p, const Theme& t, Rect r);
void check_box(Painter& p, const Theme& t, Rect box, bool on, bool hot, bool off);
void combo_arrow(Painter& p, const Theme& t, Rect r, bool hot, bool pressed);

// The frame of a picture. `glow` draws orange around it, for someone typing.
void picture_frame(Painter& p, const Theme& t, Rect r, float glow);

// A group box's line and legend, once what is inside has been drawn.
void group_box(Painter& p, const Theme& t, Rect r, std::string_view legend);
// A blue line of text that acts as a link.
void link(Painter& p, const Theme& t, Rect r, std::string_view text, bool hot);

// A whole scrollbar. `track` is 17 wide; the arrows take 17 of it at each end.
struct ScrollbarParts {
  Rect up, down, track, thumb;
};
[[nodiscard]] ScrollbarParts scrollbar_parts(Rect bar, float view_h, float content_h, float offset);
void scrollbar(Painter& p, const Theme& t, const ScrollbarParts& parts, bool thumb_hot, bool up_hot, bool down_hot);

// The window's own frame and caption, for a window of this size.
void window_frame(Painter& p, const Theme& t, float width, float height, bool active);
void caption_text(Painter& p, const Theme& t, Rect r, std::string_view text);
enum class CaptionGlyph : std::uint8_t { Minimize, Close };
void caption_button(Painter& p, const Theme& t, Rect r, CaptionGlyph glyph, bool hot, bool pressed);

}  // namespace crpp::ui::luna
