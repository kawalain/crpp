#pragma once
// Which keys a node may have, and what each means. The loader refuses others and the JSON Schemas
// are written from this, so what an editor offers is what a pack can say.
#include <array>
#include <span>
#include <string_view>

#include "crpp/skin/pack.hpp"

namespace crpp::skin {

enum class KeyType : unsigned char {
  Expr,    // a number or an expression
  Flag,    // true or false
  Word,    // a name of the pack's own, such as a style's or a part's
  Text,    // a text: #locale_key, @bound, or the words themselves
  List,    // a list of nodes
  Exprs,   // a list of expressions
  Words,   // a list of names
  Args,    // names with expressions
  Caps,    // "round" or "flat"
  Yes,     // always true
  Screen,  // a name the screen offers
  Curve,   // "ease" or "linear"
  Jump,    // "jump"
};

struct KeyDoc {
  std::string_view key;
  KeyType type;
  std::string_view doc;
};

inline constexpr KeyDoc common_keys[] = {
    {"if", KeyType::Expr, "Draw this only when it is not 0. Its id then reads as zeros."},
    {"id", KeyType::Word, "A name for its box: siblings further on read id.x, .y, .w, .h, .right and .bottom."},
    {"x", KeyType::Expr, "From the left of the box around it. 0 when left out."},
    {"right", KeyType::Expr, "Where its right edge goes, instead of x."},
    {"y", KeyType::Expr, "From the top of the box around it. 0 when left out, or flow in a column."},
    {"bottom", KeyType::Expr, "Where its bottom edge goes, instead of y."},
    {"w", KeyType::Expr, "Its width. The box around it, or a part's own w, when left out."},
    {"h", KeyType::Expr, "Its height. The box around it, or a part's own h, when left out."},
    {"after", KeyType::Expr, "In a column: how much further down the next one goes."},
    {"hit", KeyType::Screen, "A name for clicks. What is inside reads hot and pressed. It goes on top of what was drawn before it, after this node's own children."},
    {"hit_index", KeyType::Expr, "Added to the id of hit. i in a repeat when left out."},
    {"hit_by", KeyType::Expr, "The id of a click as a number, instead of hit, for a part: a screen gives it with hit('name'). Less than 0 is none."},
    {"live", KeyType::Expr, "0: what is inside is never hot or pressed."},
    {"hit_if", KeyType::Expr, "0: it takes no clicks."},
    {"note", KeyType::Screen, "Its box goes to the screen under this name."},
    {"note_by", KeyType::Expr, "note as a number, for a part: a screen gives it with note('name'). Less than 0 is none."},
    {"snap", KeyType::Flag, "Its box moves to whole pixels."},
    {"alpha", KeyType::Expr, "The colour's alpha, from 0 to 1, on fill, frame, stroke and text."},
    {"hit_path", KeyType::Word, "SVG path data: the hit takes this shape inside its box."},
    {"hit_path_x", KeyType::Expr, "Where that shape's origin is, from the left of the box around it."},
    {"hit_path_y", KeyType::Expr, "Where that shape's origin is, from the top of the box around it."},
};

inline constexpr KeyDoc fill_keys[] = {
    {"radius", KeyType::Expr, "Rounds its corners."},
    {"down_to", KeyType::Expr, "How far down from its top a gradient ends, instead of its height."},
    {"across", KeyType::Flag, "A gradient runs from left to right over its box, not downward. It takes no radius or down_to."},
    {"ellipse", KeyType::Flag, "Fills the ellipse in its box."},
};
inline constexpr KeyDoc stroke_keys[] = {
    {"radius", KeyType::Expr, "Rounds its corners."},
    {"width", KeyType::Expr, "How thick its line is."},
    {"ellipse", KeyType::Flag, "Around the ellipse in its box, and no radius."},
};
inline constexpr KeyDoc shadow_keys[] = {
    {"radius", KeyType::Expr, "The radius of the box it is under."},
    {"blur", KeyType::Expr, "As CSS's box-shadow."},
    {"dy", KeyType::Expr, "How far down it goes."},
};
inline constexpr KeyDoc line_keys[] = {
    {"x2", KeyType::Expr, "Where it ends, from the left of the box around it."},
    {"y2", KeyType::Expr, "Where it ends, from the top of the box around it."},
    {"width", KeyType::Expr, "How thick it is."},
};
inline constexpr KeyDoc path_keys[] = {
    {"color", KeyType::Expr, "A Palette entry, as $name."},
    {"scale", KeyType::Expr, "Its size. 1 when left out."},
    {"width", KeyType::Expr, "Strokes it this thick, instead of filling it."},
    {"caps", KeyType::Caps, "The ends of a stroke."},
    {"soft", KeyType::Flag, "Smooth edges under a Skin whose edges are whole pixels."},
    {"from_y", KeyType::Expr, "With a color that is a gradient: where it starts, in the path's own coordinates."},
    {"to_y", KeyType::Expr, "Where that gradient ends."},
    {"blur", KeyType::Expr, "Draws this shape's soft shadow in the color, instead of the shape."},
    {"dy", KeyType::Expr, "How far down that shadow goes, before the path's scale."},
};
inline constexpr KeyDoc text_keys[] = {
    {"style", KeyType::Word, "A style of the pack."},
    {"color", KeyType::Expr, "A Palette entry, as $name."},
    {"size", KeyType::Expr, "Instead of the style's, where it depends on the screen."},
    {"line_height", KeyType::Expr, "Instead of the style's, where it depends on the screen."},
};
inline constexpr KeyDoc picture_keys[] = {
    {"opacity", KeyType::Expr, "From 0 to 1."},
    {"gray", KeyType::Expr, "How far it goes to gray, from 0 to 1."},
};
inline constexpr KeyDoc field_keys[] = {
    {"style", KeyType::Word, "A style of the pack."},
    {"color", KeyType::Expr, "A Palette entry for its text, as $name."},
    {"placeholder", KeyType::Text, "What it shows when empty."},
    {"placeholder_style", KeyType::Word, "A style for that, instead of its own."},
    {"placeholder_color", KeyType::Expr, "A Palette entry, as $name."},
    {"caret", KeyType::Expr, "A Palette entry, as $name."},
    {"selection", KeyType::Expr, "A Palette entry, as $name."},
};
inline constexpr KeyDoc slot_keys[] = {
    {"numbers", KeyType::Exprs, "Numbers for the screen, at most 8."},
    {"styles", KeyType::Words, "Styles for the screen, at most 8."},
    {"colors", KeyType::Exprs, "Palette entries for the screen, at most 8."},
};
inline constexpr KeyDoc clip_keys[] = {
    {"clip", KeyType::Flag, "Nothing inside shows outside its box."},
};
inline constexpr KeyDoc group_keys[] = {
    {"clip", KeyType::Flag, "Nothing inside shows outside its box."},
    {"opacity", KeyType::Expr, "Draws what is inside as one, then blends it at this, from 0 to 1. What falls outside its box is cut."},
    {"fade_top", KeyType::Expr, "What is inside fades out over this length at the top of its box."},
    {"fade_bottom", KeyType::Expr, "What is inside fades out over this length at the bottom of its box."},
    {"scale", KeyType::Expr, "What is inside grows by this, 1 being its own size. Its hits stay where they are."},
    {"about_x", KeyType::Expr, "What stays still when it grows, from the left of the box around it. The middle of its box when left out."},
    {"about_y", KeyType::Expr, "What stays still when it grows, from the top of the box around it. The middle of its box when left out."},
};
inline constexpr KeyDoc use_keys[] = {
    {"args", KeyType::Args, "What the part's arguments are here."},
    {"text", KeyType::Text, "What the part reads as @label."},
    {"draw", KeyType::List, "What goes where the part has a node {\"content\": true}."},
};
inline constexpr KeyDoc repeat_keys[] = {
    {"draw", KeyType::List, "Runs that many times, with i counting from 0."},
};
inline constexpr KeyDoc value_keys[] = {
    {"value", KeyType::Expr, "The number."},
};
inline constexpr KeyDoc let_keys[] = {
    {"value", KeyType::Expr, "The number."},
    {"to", KeyType::Expr, "Instead of value: this name is a transition, running from 0 to 1 and back as this goes on and off."},
    {"ms", KeyType::Expr, "How long a transition takes."},
    {"ms_off", KeyType::Expr, "How long it takes going off, when that differs."},
    {"curve", KeyType::Curve, "Its curve: ease, as CSS's, when left out."},
    {"first", KeyType::Jump, "A state that is on when first seen starts at its end, as an element's first style does."},
    {"again", KeyType::Expr, "A new value of this, while on, runs it up from off again, as a new element would."},
};

struct KindDoc {
  std::string_view key;
  KeyType type;
  std::string_view doc;
  std::span<const KeyDoc> keys;
};

// In the order of NodeKind.
inline constexpr std::array<KindDoc, 18> node_kinds{{
    {"fill", KeyType::Expr, "Fills its box with a Palette entry, plain or a gradient.", fill_keys},
    {"frame", KeyType::Expr, "A line inside the edge of its box, in a Palette entry.", {}},
    {"stroke", KeyType::Expr, "A rounded line around its box, in a Palette entry.", stroke_keys},
    {"line", KeyType::Expr, "A line in a Palette entry, from x, y to x2, y2.", line_keys},
    {"path", KeyType::Word, "SVG path data, drawn from x, y.", path_keys},
    {"text", KeyType::Text, "A text: #locale_key, @bound, or the words themselves.", text_keys},
    {"picture", KeyType::Screen, "A picture the screen supplies.", picture_keys},
    {"field", KeyType::Screen, "A text field the screen owns.", field_keys},
    {"slot", KeyType::Screen, "What only the screen can draw, in this box.", slot_keys},
    {"group", KeyType::List, "A list of nodes inside this box.", group_keys},
    {"column", KeyType::List, "A list of nodes under each other: each goes at y = flow, and flow moves below it, by its after as well. Its height is what they took.", clip_keys},
    {"use", KeyType::Word, "A part of pack.jsonc.", use_keys},
    {"content", KeyType::Yes, "Where a part puts the draw list of whoever uses it.", {}},
    {"repeat", KeyType::Expr, "How many times its draw list runs.", repeat_keys},
    {"let", KeyType::Word, "A name for a number, for what comes after it and inside that.", let_keys},
    {"set", KeyType::Word, "A new value for a name above it, as when a repeat looks for the widest.", value_keys},
    {"report", KeyType::Screen, "A number for the screen.", value_keys},
    {"shadow", KeyType::Expr, "A soft shadow under a rounded box, in a Palette entry.", shadow_keys},
}};

}  // namespace crpp::skin
