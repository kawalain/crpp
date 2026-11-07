#pragma once
// A Skin Pack as it stays in memory: a compact tree of nodes, with the JSON it came from gone.
// Everything a pack names is checked when it loads, so that drawing has nothing left to refuse.
//
// pack.json holds format, palettes (one per Theme id), styles and parts; screens/<name>.json
// holds a screen as {"draw": [nodes]}. Each node is one thing to draw, chosen by which of these
// keys it has:
//   fill    a Palette entry, plain or a gradient, with an optional radius
//   frame   a line inside the box's edge
//   stroke  a rounded line, with radius and width
//   text    a text: #locale_key, @bound, or the words themselves, in a style and a color
//   picture a picture the screen supplies
//   field   a text field the screen owns, with style, color, placeholder, placeholder_color,
//           caret and selection
//   group   a list of nodes, inside this box, with an optional clip
//   use     a part from pack.json, with args and an optional text, which the part reads as @label
//   repeat  a number; its "draw" runs that many times, with i counting
//   let     a name for a number, with value
// and each may have: if, id (siblings can then read id.x, .y, .w, .h, .right, .bottom), x, y,
// w, h (x and y from the box around it, 0 and that box's size by default), hit (a name for
// clicks, counted up by i), snap (the box moves to whole pixels), and "//" for a comment.
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "crpp/skin/color.hpp"
#include "crpp/skin/expr.hpp"

namespace crpp::skin {

// What a pack says it is written for; another number is refused.
inline constexpr int pack_format = 1;

// A pack can be a download, so nothing in one is taken without a limit.
inline constexpr std::size_t max_file_bytes = 1024 * 1024;
inline constexpr std::size_t max_nodes = 50000;
inline constexpr std::size_t max_instructions = 500000;
inline constexpr int max_node_depth = 32;
inline constexpr int max_frame_slots = 1024;
inline constexpr int max_repeat = 4096;
// However parts and repeats multiply: what one walk of a screen goes through at most.
inline constexpr int max_walk = 200000;
inline constexpr int max_walk_depth = 64;

// What a screen offers a pack, under the names it may use. The ids are the screen's own.
struct Name {
  std::string_view name;
  int id = 0;
};

struct ScreenSchema {
  std::string_view screen;
  std::span<const Name> texts, numbers, pictures, fields, hits;
};

enum class NodeKind : std::uint8_t { Fill, Frame, Stroke, Text, Picture, Field, Group, Use, Repeat, Let };

inline constexpr std::uint16_t no_slot = std::numeric_limits<std::uint16_t>::max();
inline constexpr std::uint32_t no_text = std::numeric_limits<std::uint32_t>::max();
inline constexpr int no_hit = -1;

struct Node {
  NodeKind kind = NodeKind::Group;
  bool snap = false, clip = false, has_if = false;
  std::uint16_t slot = no_slot;  // where id's box goes, or a let's value
  std::uint16_t style = 0;
  int hit = no_hit;
  int bound = 0;                 // Picture, Field: the screen's id
  std::uint32_t text = no_text;  // Text
  std::uint32_t first = 0, count = 0;  // Group, Repeat: children. Use: index into `uses`. Field: index into `fields`
  Expr when, x, y, w, h;
  Expr paint;  // Fill, Frame, Stroke, Text
  Expr extra;  // Fill, Stroke: radius. Repeat: how many. Let: value
  Expr width;  // Stroke
};

struct Style {
  Expr size, line_height;
  int weight = 400;
  std::uint8_t align = 0;  // 0 left, 1 center, 2 right
  bool wrap = false, ellipsis = false, exact = false, keep_words = false;
};

struct TextRef {
  enum class Kind : std::uint8_t { Literal, Locale, Bound, Label };
  Kind kind = Kind::Literal;
  std::uint32_t a = 0, b = 0;  // Literal: offset and size. Locale: its key. Bound: the screen's id
};

struct Argument {
  std::uint16_t slot = 0;
  Expr value;
};

struct Part {
  std::string name;
  std::uint32_t first = 0, count = 0;
  std::uint16_t frame = 0, deepest = 0;  // slots of its own; with the parts it uses
  bool snap = false;
};

struct Use {
  std::uint32_t part = 0, first_argument = 0, arguments = 0;
  std::uint32_t label = no_text;
};

struct FieldNode {
  Expr placeholder_color, caret, selection;
  std::uint32_t placeholder = no_text;
};

struct Screen {
  std::string name;
  std::uint32_t first = 0, count = 0;
  std::uint16_t frame = 0, deepest = 0;
};

// One Palette's entry: a colour, or stops when there are any.
struct Paint {
  Color color;
  std::uint32_t first_stop = 0, stops = 0;
};

struct Pack {
  std::string id;
  std::vector<Node> nodes;
  std::vector<Instr> code;
  std::vector<Style> styles;
  std::vector<TextRef> texts;
  std::string literals;
  std::vector<Part> parts;
  std::vector<Use> uses;
  std::vector<Argument> arguments;
  std::vector<FieldNode> fields;
  std::vector<Screen> screens;
  std::vector<std::string> palette_ids;
  std::vector<Paint> paints;  // palette after palette, each with paint_names entries
  std::vector<GradientStop> stops;
  std::uint32_t paint_names = 0;

  [[nodiscard]] std::optional<int> palette_of(std::string_view theme_id) const;
  [[nodiscard]] const Screen* screen(std::string_view name) const;
  [[nodiscard]] const Paint* paint(int palette, float value) const;
};

// Reads a file of the pack by its path inside it. Empty when there is none.
using ReadFile = std::function<std::optional<std::string>(std::string_view path)>;

// A screen with no file is left out, and that screen can tell from Pack::screen.
[[nodiscard]] std::expected<Pack, std::string> load_pack(const ReadFile& read, std::span<const ScreenSchema> schemas);

}  // namespace crpp::skin
