#pragma once
// A Skin Pack as it stays in memory: a compact tree of nodes, with the text it came from gone.
// Everything a pack names is checked when it loads, so that drawing has nothing left to refuse.
//
// Its files are in the pack language, which read_cskin (cskin.hpp) reads into a tree and skins/README.md
// teaches. pack.cskin holds format, constants (names for numbers, anywhere in the pack), fonts, palettes (one per
// Theme id), styles and parts; screens/<name>.cskin holds a screen's nodes. A Palette entry is #rrggbb, #rrggbbaa,
// stops(at: colour, ...), mix(a, b, N%) or alpha(a, value) of an entry above it.
//
// Each node is one thing to draw, chosen by the key it starts with; a node is written `key value key value ...`.
// The keys, as the tree has them:
//   fill    a Palette entry, plain or a gradient, with an optional radius; a gradient runs over the
//           box unless down_to says how far down it ends, or from left to right with across.
//           Fill, frame, stroke and text take alpha. With ellipse it fills the ellipse in its box
//   frame   a line inside the box's edge
//   stroke  a rounded line, with radius and width, or around the ellipse in its box
//   shadow  a soft shadow under a rounded box, with radius, blur and dy, as CSS's box-shadow
//   line    a colour, from x, y to x2, y2 with width
//   path    SVG path data, drawn from x, y, with color, scale, and a width to stroke it instead of
//           filling, with caps round (the default) or flat, and soft for smooth edges
//           under a Skin whose edges are whole pixels. A color that is a gradient runs from from_y to to_y
//           of the path's own coordinates. With blur it is that shape's shadow, dy further down. With scale_y it is
//           filled at that size downward and scale across. Data that starts with F1 fills where its shapes overlap
//   text    a text: #locale_key, @bound, or the words themselves, in a style and a color, and
//           size and line_height to change the style's where it depends on the screen
//   picture a picture the screen supplies, with opacity and gray
//   field   a text field the screen owns, with style, color, placeholder, placeholder_color,
//           caret and selection
//   slot    what only the screen can draw, given numbers, styles and colors
//   group   a list of nodes, inside this box, with an optional clip, whose edge has partial pixels with
//           soft_clip. opacity draws them as one, at that
//           opacity. fade_top and fade_bottom fade what they hold out over that length at those edges.
//           scale grows them around about_x, about_y, or the middle of the box, or with origin from
//           its corner, which they then count from. dy draws them further down
//   column  a list of nodes under each other: each goes at y = flow unless it says otherwise, and
//           flow then moves below it, by its after as well. Its height is what they took
//   use     a part from pack.cskin, with args, a text the part reads as @label, and nodes in { }
//           that go where the part has a node `content`
//   content see use
//   repeat  a number; its nodes in { } run that many times, with @i counting
//   let     `let name = value`. `let name = ease(to, ms)` is a transition: it runs from 0 to 1 and back as to
//           goes on and off, over ms (ms_off when going off, if it says), on a curve: ease, the
//           default, or linear. first jump has a state that is on when first seen start at its end.
//           A new value of again starts it anew: on, it runs up from off; off, it is off at once. In a repeat it
//           belongs to its round, or with key to that number, as someone whose place in a list can change.
//   set     `set name = value`, a new value for a name above it, as when a repeat looks for the widest
//   report  `report name = value`, a number for the screen
// and each may have: if, id (siblings can then read id.x, .y, .w, .h, .right, .bottom), x or right,
// y or bottom, w, h (x and y from the box around it, 0 and that box's size by default), after,
// hit (a name for clicks, counted up by hit_index, or by i if there is none), live (false: it is not
// lit), hit_if (false: no hit), tab_order (where the keyboard comes to it), note (the box goes to the screen under this name), snap (the box moves
// to whole pixels), hit_path (the hit takes the shape of this path, set at hit_path_x and hit_path_y).
// A part knows no screen, so it takes hit_by and note_by: a
// number, which a screen gives it as an argument with hit(name) or note(name), and less than 0 for none.
// A note_by that is not one of the screen's notes reaches nothing.
//
// cursors names, for all seven of cursor_names, a part that draws it in a box of 32 by 32, and its hot
// point there. A pack without them leaves the pointer to the system. With hard they are drawn with
// hard edges in that box, then enlarged by whole pixels, so the steps of the grid are part of the look.
//
// notification_sounds, when not 0, has a message and someone entering sound as tones made by the program:
// 1 small bells, 2 plain tones.
//
// Some constants are read by the program too: see required_constants. Fonts are installed ones, the
// first of each list that this system has: korean for Korean and for every Hangul letter, other for
// the other languages, caption for a style of role caption, with the size that font_size then is.
//
// A part may give w and h for a use that says none, and height: worked out after it has drawn,
// from its own names, it is the height it took in a column.
//
// A pack may say "extends": an id, its parent. It then gets all of the parent's, and what it names itself replaces
// what the parent has by that name: a constant, style, part, Palette, Theme, screen, or fonts or cursors as a whole.
// Nothing inside a part or screen is patched. An overlay is the same thing inside a pack: overlays/<name>/pack.cskin
// and its screens/, put over the pack when a Theme asks for it.
//
// names says what a user calls this Skin, by locale id. themes lists what a user can pick: names by locale, its palette (its id when it says none), its dark variant if it
// has one, and its overlay if it needs one. A pack is read for one overlay at a time, and holds only the Palettes of the
// Themes of that overlay; a pack without themes holds all it has. default_theme is the Theme to go to.
// A Palette may say "extends": another Palette, whose entries it starts from.
#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "crpp/skin/color.hpp"
#include "crpp/skin/expr.hpp"

namespace crpp::skin {

// What a pack says it is written for. This exe reads the formats from oldest_pack_format to
// pack_format, and the official packs are written in pack_format. Each format has a catalog
// address of its own (ADR 0009), so an older exe never meets a pack it would refuse.
inline constexpr int oldest_pack_format = 1;
inline constexpr int pack_format = 2;
[[nodiscard]] constexpr bool reads_format(std::int64_t format) { return format >= oldest_pack_format && format <= pack_format; }

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
inline constexpr std::size_t max_slot_values = 16;
inline constexpr int max_pack_levels = 4;
inline constexpr int max_palette_levels = 8;

// What a screen offers a pack, under the names it may use. The ids are the screen's own.
struct Name {
  std::string_view name;
  int id = 0;
};

struct ScreenSchema {
  std::string_view screen;
  std::span<const Name> texts, numbers, pictures, fields, hits, slots, reports, notes;
};

enum class NodeKind : std::uint8_t { Fill, Frame, Stroke, Line, Path, Text, Picture, Field, Slot, Group, Column, Use, Content, Repeat, Let, Set, Report, Shadow };

inline constexpr std::uint16_t no_slot = std::numeric_limits<std::uint16_t>::max();
inline constexpr std::uint32_t no_text = std::numeric_limits<std::uint32_t>::max();
inline constexpr int no_hit = -1;

struct Node {
  NodeKind kind = NodeKind::Group;
  bool snap = false, clip = false, has_if = false, from_right = false, from_bottom = false;
  bool auto_h = false;      // Column: h is what its nodes took
  bool has_hit_index = false, has_live = false, has_hit_if = false, has_after = false, has_tab_order = false;
  bool round_caps = true;   // Path
  bool soft = false;        // Path. Group: its clip
  bool stretched = false;   // Path: it has scale_y
  bool has_note_by = false;
  bool has_size = false, has_line_height = false;  // Text. Let: has_size says it has again
  bool has_alpha = false;   // Let: it has a key
  bool has_down_to = false;  // Fill
  bool across = false;       // Fill
  bool ellipse = false;      // Fill, Stroke
  bool shadow = false;       // Path
  bool has_stops = false;    // Path
  bool has_effects = false;  // Group
  bool transition = false, first_jumps = false, linear = false;  // Let
  std::uint16_t slot = no_slot;  // where id's box goes, or a let's value
  std::uint16_t style = 0;
  int hit = no_hit;
  int bound = 0;                 // Picture, Field, Slot, Report: the screen's id
  int note = no_hit;             // the screen's id for the box
  std::uint32_t text = no_text;  // Text. Path: offset of its data in literals
  std::uint32_t first = 0, count = 0;  // Group, Column, Repeat: children. Use: index into uses. Field: fields. Slot: slots. Path: size of its data
  std::uint32_t more = no_text;  // Group: index into effects. Path: into path_extras
  std::uint32_t hit_shape = no_text;  // index into hit_shapes
  Expr when, x, y, w, h;
  Expr paint;  // Fill, Frame, Stroke, Line, Path, Text
  Expr extra;  // Fill, Stroke, Shadow: radius. Line: x2. Path: scale. Picture: opacity. Repeat: how many. Let: value, or to. Report: value
  Expr width;  // Stroke, Line, Path. Fill: down_to. Picture: gray. Let: ms. Shadow: blur
  Expr extra2;  // Line: y2. Let: ms_off. Shadow: dy
  Expr after, hit_index, live, hit_if, alpha, note_by, tab_order;
  Expr size, line_height;  // Text. Let: size is again
};

struct GroupEffects {
  bool has_scale = false, has_about_x = false, has_about_y = false, has_opacity = false, has_fade = false, origin = false, has_dy = false;
  Expr scale, about_x, about_y, opacity, fade_top, fade_bottom, dy;
};

struct PathExtra {
  Expr from_y, to_y;  // a gradient
  Expr dy;            // a shadow
  Expr scale_y;       // filled at another size downward
};

struct HitShape {
  std::uint32_t data = 0, size = 0;  // in literals
  Expr x, y;
};

struct Style {
  Expr size, line_height;
  float letter_spacing = 0;
  int weight = 400;
  std::uint8_t align = 0;  // 0 left, 1 center, 2 right
  std::uint8_t role = 0;   // 0 body, 1 caption
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
  std::vector<std::string> argument_names;  // their slots count up from 0
  std::vector<Expr> argument_defaults;
  std::uint32_t first = 0, count = 0;
  std::uint16_t frame = 0;  // slots of its own
  bool snap = false, has_w = false, has_h = false, has_height = false;
  Expr w, h, height;
};

struct Use {
  std::uint32_t part = 0, first_argument = 0, arguments = 0;
  std::uint32_t label = no_text;
  std::uint32_t first = 0, count = 0;  // what goes where the part says content
};

struct FieldNode {
  Expr placeholder_color, caret, selection;
  std::uint32_t placeholder = no_text;
  bool has_placeholder_style = false;
  std::uint16_t placeholder_style = 0;
};

struct SlotNode {
  std::uint32_t first_number = 0, numbers = 0, first_style = 0, styles = 0, first_color = 0, colors = 0;
};

struct Screen {
  std::string name;
  std::uint32_t first = 0, count = 0;
  std::uint16_t frame = 0;
  // The ids of the screen's notes: a note_by reaches the screen only under one of them.
  std::vector<int> notes;
};

// One Palette's entry: a colour, or stops when there are any.
struct Paint {
  Color color;
  std::uint32_t first_stop = 0, stops = 0;
};

enum class CursorKind : std::uint8_t { Arrow, Hand, IBeam, SizeWE, SizeNS, SizeNWSE, SizeNESW };
inline constexpr std::array<std::string_view, 7> cursor_names = {"arrow", "hand", "ibeam", "size_we", "size_ns", "size_nwse", "size_nesw"};
// The box a cursor's part draws in.
inline constexpr float cursor_box = 32;

struct PackCursor {
  std::uint32_t part = 0;
  float hot_x = 0, hot_y = 0;
};

struct PackFonts {
  std::vector<std::string> korean, other, caption;
  float korean_size = 0, other_size = 0;
};

struct PackTheme {
  std::string id, palette, dark, overlay;  // dark and overlay are empty when it has none
  std::vector<std::pair<std::string, std::string>> names;  // by locale id
};

// What a pack says of the pack it extends besides its id: where it is, and what it must be.
struct ParentPin {
  std::string id;
  std::string repository;   // an address to add its Repository by, or empty
  std::string fingerprint;  // of its Repository's key, 64 lowercase hex; empty: the same or the official one
  std::string version;      // 0.y or 0.y.z: that version, or any 0.y.z; empty: any
  std::string sha256;       // the digest of its own files in its catalog, or empty
};

// 0.y.z, as a pack says its own version.
[[nodiscard]] bool is_version(std::string_view s);
// 0.y or 0.y.z, as a pin asks for one.
[[nodiscard]] bool is_version_pin(std::string_view s);

struct Pack {
  std::string id;
  std::string version;            // empty when it says none
  std::optional<ParentPin> pin;   // the pack it extends, if any
  std::string overlay;  // the one this was read for, or empty
  std::vector<std::pair<std::string, std::string>> names;  // what a user calls this Skin, by locale id
  std::vector<PackTheme> themes;
  std::string default_theme;
  std::optional<PackFonts> fonts;
  std::optional<std::array<PackCursor, cursor_names.size()>> cursors;
  bool cursors_hard = false;
  std::vector<Node> nodes;
  std::vector<GroupEffects> effects;
  std::vector<PathExtra> path_extras;
  std::vector<HitShape> hit_shapes;
  std::vector<Instr> code;
  std::vector<Style> styles;
  std::vector<TextRef> texts;
  std::string literals;
  std::vector<Part> parts;
  std::vector<Use> uses;
  std::vector<Argument> arguments;
  std::vector<FieldNode> fields;
  std::vector<SlotNode> slots;
  std::vector<Expr> slot_values;
  std::vector<std::uint16_t> slot_styles;
  std::vector<Screen> screens;
  std::vector<std::string> palette_ids, paint_names_list;
  std::vector<std::pair<std::string, float>> constants;
  std::vector<Paint> paints;  // palette after palette, each with paint_names entries
  std::vector<GradientStop> stops;
  std::uint32_t paint_names = 0;

  [[nodiscard]] std::optional<int> palette_of(std::string_view theme_id) const;
  // The Theme a Palette belongs to, as itself or as its dark variant.
  [[nodiscard]] const PackTheme* theme_of_palette(std::string_view palette) const;
  // Is this Palette the dark variant of a Theme?
  [[nodiscard]] bool palette_is_dark(int palette) const;
  [[nodiscard]] const Screen* screen(std::string_view name) const;
  [[nodiscard]] const Part* part(std::string_view name) const;
  [[nodiscard]] const Paint* paint(int palette, float value) const;
  // By name, for what a screen's code asks of the pack. Zero and null when it has none.
  [[nodiscard]] float constant(std::string_view name) const;
  [[nodiscard]] const Paint* paint_named(int palette, std::string_view name) const;
};

// Reads a file of the pack by its path inside it. Empty when there is none.
using ReadFile = std::function<std::optional<std::string>(std::string_view path)>;

// A pack extends another: its id, its Repository's fingerprint when it names one, and where the
// pack that extends it was found, in the caller's own words.
struct ParentQuery {
  std::string_view id, fingerprint, from;
};
struct FoundPack {
  ReadFile read;
  std::string where;  // what its own parent is looked for from
};

struct LoadOptions {
  std::string overlay;  // to read the pack with
  std::string where;    // where the pack itself is, for ParentQuery::from
  std::function<std::optional<FoundPack>(const ParentQuery& query)> parent;  // finds the pack a pack extends
};

// A screen with no file is left out, and that screen can tell from Pack::screen.
[[nodiscard]] std::expected<Pack, std::string> load_pack(const ReadFile& read, std::span<const ScreenSchema> schemas, const LoadOptions& options = {});

// The constants the program reads, as a pack that is to be used must have them. A pack whose
// own_frame is not 0 draws its own window frame, and also needs own_frame_constants.
[[nodiscard]] std::span<const std::string_view> required_constants();
[[nodiscard]] std::span<const std::string_view> own_frame_constants();
// And one whose moves is not 0 says these, of the motion that stays in the engine.
[[nodiscard]] std::span<const std::string_view> moving_constants();
// The first of them this pack lacks.
[[nodiscard]] std::optional<std::string_view> missing_constant(const Pack& pack);

}  // namespace crpp::skin
