#include <map>
#include "crpp/skin/pack.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "check.hpp"
#include "crpp/locale/locale.hpp"
#include "crpp/skin/draw.hpp"

using namespace crpp;
using namespace crpp::skin;

namespace {

constexpr Name test_texts[] = {{"nickname", 11}, {"seat", 12}};
constexpr Name test_numbers[] = {{"seats", 21}, {"twice", 22}};
constexpr Name test_pictures[] = {{"me", 31}};
constexpr Name test_fields[] = {{"code", 41}};
constexpr Name test_hits[] = {{"join", 51}, {"seat", 100}};
constexpr Name test_slots[] = {{"list", 61}};
constexpr Name test_reports[] = {{"height", 71}};
constexpr Name test_notes[] = {{"where", 81}};
constexpr ScreenSchema test_schema{"home", test_texts, test_numbers, test_pictures, test_fields, test_hits, test_slots, test_reports, test_notes};

const char* const test_pack = R"C(pack test format 1


palette one {
  face #ff0000
  ink #00000080
  shade stops(0: #000000, 1: #ffffff)
}

palette two {
  shade stops(0: #111111, 1: #222222)
  ink #0000ff
  face #00ff00
}

style body size @font_size line_height @h ellipsis exact
style big size 20 line_height @font_size+4 weight 700 align center wrap


part by(own = -1, where = -1) {
  hit_by own note_by where {
    fill @hot ? ink : face
  }
}

part inner(amount = 1) {
  fill face w amount
}

part row h 20 w textw(body, @label) {
  text @label style body color face
}

part box height rows.bottom+6 {
  column id rows x 10 y 14 w @W-20 {
    content
  }
  frame ink h rows.bottom+6
}

part button(off = 0) snap {
  let gap = off ? 2 : 3
  fill @pressed ? ink : face radius 3
  text @label style body color @hot ? ink : face x gap
  use inner(amount = gap*10)
}
)C";

ReadFile files(std::string pack, std::string home) {
  return [pack = std::move(pack), home = std::move(home)](std::string_view path) -> std::optional<std::string> {
    if (path == "pack.cskin") return pack;
    if (path == "screens/home.cskin" && !home.empty()) return home;
    return std::nullopt;
  };
}

std::expected<Pack, std::string> load(std::string_view nodes) {
  return load_pack(files(test_pack, std::string(nodes)), std::span(&test_schema, 1));
}

class Log final : public Canvas {
 public:
  std::string out;
  int hot_id = -1, pressed_id = -1;

  void add(std::string line) { out += std::move(line) + "\n"; }
  static std::string str(float v) {
    std::string s = std::to_string(v);
    s.erase(s.find_last_not_of('0') + 1);
    if (s.back() == '.') s.pop_back();
    return s;
  }
  static std::string str(Box b) { return str(b.x) + "," + str(b.y) + "," + str(b.w) + "," + str(b.h); }
  static std::string str(Color c) { return str(c.r) + "/" + str(c.g) + "/" + str(c.b) + "/" + str(std::round(c.a * 100) / 100); }

  Box snap(Box b) override { return {std::round(b.x), std::round(b.y), b.w, b.h}; }
  float snap(float v) override { return std::round(v); }
  float line() override { return 2; }
  float font_size() override { return 12; }
  float text_width(std::string_view text, const TextLook&, float width) override { return std::min(static_cast<float>(text.size()) * 10, width); }
  float text_height(std::string_view text, const TextLook& look, float width) override { return std::ceil(static_cast<float>(text.size()) * 10 / width) * look.line_height; }
  void fill(Box b, Color c) override { add("fill " + str(b) + " " + str(c)); }
  void fill_down(Box b, float y0, float y1, std::span<const GradientStop> s) override { add("down " + str(b) + " " + str(y0) + ".." + str(y1) + " " + std::to_string(s.size())); }
  void fill_across(Box b, std::span<const GradientStop> s) override { add("across " + str(b) + " " + std::to_string(s.size())); }
  void fill_round(Box b, float r, Color c) override { add("round " + str(b) + " " + str(r) + " " + str(c)); }
  void fill_round_down(Box b, float r, float y0, float y1, std::span<const GradientStop> s) override { add("rounddown " + str(b) + " " + str(r) + " " + str(y0) + ".." + str(y1) + " " + std::to_string(s.size())); }
  void frame(Box b, Color c) override { add("frame " + str(b) + " " + str(c)); }
  void stroke_round(Box b, float r, Color c, float w) override { add("stroke " + str(b) + " " + str(r) + " " + str(c) + " " + str(w)); }
  void line(float x1, float y1, float x2, float y2, Color c, float w) override { add("line " + str(x1) + "," + str(y1) + " " + str(x2) + "," + str(y2) + " " + str(c) + " " + str(w)); }
  void path(std::string_view d, float x, float y, float s, Color c, float w, bool round_caps, bool) override { add("path " + std::string(d) + " " + str(x) + "," + str(y) + " x" + str(s) + " " + str(c) + " " + str(w) + (round_caps ? " round" : " flat")); }
  void text(std::string_view t, Box b, const TextLook& l, Color c) override {
    add("text " + std::string(t) + " " + str(b) + " " + str(l.size) + "/" + str(l.line_height) + "/" + std::to_string(l.weight) + " " + str(c));
  }
  void push_clip(Box b) override { add("clip " + str(b)); }
  void push_soft_clip(Box b) override { add("softclip " + str(b)); }
  void push_scale_at(float x, float y, float s) override { add("scale at " + str(x) + "," + str(y) + " x" + str(s)); }
  void push_down(float dy) override { add("down " + str(dy)); }
  void path_stretched(std::string_view d, float x, float y, float sx, float sy, Color c) override { add("stretched " + std::string(d) + " " + str(x) + "," + str(y) + " x" + str(sx) + " y" + str(sy) + " " + str(c)); }
  void pop_clip() override { add("unclip"); }
  void hit(Box b, int id) override { add("hit " + str(b) + " " + std::to_string(id)); }
  void hit_shape(Box b, int id, std::string_view d, float x, float y) override { add("hitshape " + str(b) + " " + std::to_string(id) + " " + std::string(d) + " " + str(x) + "," + str(y)); }
  float transition(std::uint64_t key, const TransitionSpec& s) override {
    keys.push_back(key);
    add("transition " + std::string(s.on ? "on " : "off ") + str(s.ms_on) + "/" + str(s.ms_off) + (s.first_jumps ? " jump" : "") + (s.curve == Curve::Linear ? " linear" : "") + (s.again != 0 ? " again " + str(s.again) : ""));
    return 0.5f;
  }
  void fill_ellipse(Box b, Color c) override { add("ellipse " + str(b) + " " + str(c)); }
  void stroke_ellipse(Box b, Color c, float w) override { add("ring " + str(b) + " " + str(c) + " " + str(w)); }
  void shadow_round(Box b, float r, float blur, float dy, Color c) override { add("shadow " + str(b) + " " + str(r) + " " + str(blur) + " " + str(dy) + " " + str(c)); }
  void path_gradient(std::string_view d, float x, float y, float s, float y0, float y1, std::span<const GradientStop> stops) override {
    add("pathdown " + std::string(d) + " " + str(x) + "," + str(y) + " x" + str(s) + " " + str(y0) + ".." + str(y1) + " " + std::to_string(stops.size()));
  }
  void path_shadow(std::string_view d, float x, float y, float s, float blur, float dy, Color c) override {
    add("pathshadow " + std::string(d) + " " + str(x) + "," + str(y) + " x" + str(s) + " " + str(blur) + " " + str(dy) + " " + str(c));
  }
  void push_scale(float x, float y, float s) override { add("scale " + str(x) + "," + str(y) + " x" + str(s)); }
  void pop_scale() override { add("unscale"); }
  bool push_opacity(Box b, float o) override {
    add("opacity " + str(b) + " " + str(o));
    return o < 1;
  }
  bool push_edge_fade(Box b, float top, float bottom) override {
    add("fade " + str(b) + " " + str(top) + " " + str(bottom));
    return top > 0 || bottom > 0;
  }
  void pop_layer() override { add("unlayer"); }
  std::vector<std::uint64_t> keys;
  bool hot(int id) override { return id == hot_id; }
  bool pressed(int id) override { return id == pressed_id; }
};

class TestSource final : public Source {
 public:
  Log* log = nullptr;
  int seats = 0;
  std::vector<std::string> names{"A", "B", "C"};
  std::string_view text(int id, int index) override {
    if (id == 11) return "Me";
    if (id == 12) return names[static_cast<std::size_t>(index)];
    return {};
  }
  float number(int id, int index) override { return id == 21 ? static_cast<float>(seats) : id == 22 ? static_cast<float>(index * 2) : 0; }
  void picture(int id, int, Box b, float opacity, float gray) override { log->add("picture " + std::to_string(id) + " " + Log::str(b) + " " + Log::str(opacity) + " " + Log::str(gray)); }
  void field(int id, int, Box b, const FieldLook& l) override {
    log->add("field " + std::to_string(id) + " " + Log::str(b) + " " + std::string(l.placeholder) + " " + Log::str(l.color) + " " + Log::str(l.selection));
  }
  void slot(int id, int, Box b, const SlotValues& v) override {
    log->add("slot " + std::to_string(id) + " " + Log::str(b) + " " + std::to_string(v.numbers.size()) + "/" + std::to_string(v.styles.size()) + "/" + std::to_string(v.colors.size()) + (v.numbers.empty() ? "" : " " + Log::str(v.numbers[0])));
  }
  void report(int id, float value) override { log->add("report " + std::to_string(id) + " " + Log::str(value)); }
  void note(int id, int index, Box b) override { log->add("note " + std::to_string(id) + "/" + std::to_string(index) + " " + Log::str(b)); }
};

std::string walk(const Pack& pack, int palette = 0, int seats = 0, int hot = -1, int pressed = -1) {
  Log log;
  log.hot_id = hot;
  log.pressed_id = pressed;
  TestSource source;
  source.log = &log;
  source.seats = seats;
  const Screen* screen = pack.screen("home");
  CHECK(screen != nullptr);
  if (screen) draw(pack, *screen, palette, {100, 200, 300, 400}, log, source);
  return log.out;
}

std::string walk(std::string_view nodes, int palette = 0, int seats = 0, int hot = -1, int pressed = -1) {
  const auto pack = load(nodes);
  if (!pack) std::fprintf(stderr, "%s\n", pack.error().c_str());
  CHECK(pack.has_value());
  return pack ? walk(*pack, palette, seats, hot, pressed) : std::string{};
}

void boxes_are_measured_from_the_box_around_them() {
  CHECK(walk(R"C(fill face)C") == "fill 100,200,300,400 1/0/0/1\n");
  CHECK(walk(R"C(fill face (10, @H-5, @W/2, 5))C") == "fill 110,595,150,5 1/0/0/1\n");
  CHECK(walk(R"C((10, 20, 100, 50) {
  fill face (1, 2, @W-2, @H-4)
}
)C") == "fill 111,222,98,46 1/0/0/1\n");
  CHECK(walk(R"C(fill face y 10 h @y*2)C") == "fill 100,210,300,20 1/0/0/1\n");
}

void siblings_read_each_others_boxes() {
  CHECK(walk(R"C(fill face (5, 6, 7, 8) id a
fill face (a.right, a.bottom, a.w, a.h)
)C") ==
        "fill 105,206,7,8 1/0/0/1\nfill 112,214,7,8 1/0/0/1\n");
  // Not a node further on, nor one in another list.
  CHECK(!load(R"C(fill face x a.x
fill face id a
)C"));
  CHECK(!load(R"C(group {
  fill face id a
}
fill face x a.x
)C"));
  CHECK(!load(R"C(fill face id a
group {
  fill face x a.x
}
)C"));
}

void a_let_is_read_further_on_and_inside() {
  CHECK(walk(R"C(let gap = @W/10
group {
  fill face x gap
}
)C") == "fill 130,200,300,400 1/0/0/1\n");
  CHECK(!load(R"C(fill face x gap
let gap = 1
)C"));
  CHECK(!load(R"C(let gap = 1
let gap = 2
)C"));
  CHECK(!load(R"C(let W = 1)C"));
}

void a_part_takes_its_hit_and_note_from_the_screen() {
  CHECK(walk(R"C(use by(own = hit(join)))C", 0, 0, 51) == "fill 100,200,300,400 0/0/0/0.5\nhit 100,200,300,400 51\n");
  // Less than 0 is none, and leaves what is lit from outside alone.
  CHECK(walk(R"C(use by)C") == "fill 100,200,300,400 1/0/0/1\n");
  CHECK(walk(R"C(use by hit join)C", 0, 0, 51) == "fill 100,200,300,400 0/0/0/0.5\nhit 100,200,300,400 51\n");
  CHECK(walk(R"C(use by(where = note(where)))C").starts_with("note 81"));
  CHECK(!load(R"C(fill face x hit(nothing))C"));
  CHECK(!load(R"C(fill face x note(nothing))C"));
}

void a_text_can_set_its_size() {
  CHECK(walk(R"C(text "x" style body color face h 10 size @h+5 line_height @h+9)C") == "text x 100,200,300,10 15/19/400 1/0/0/1\n");
}

void a_set_changes_a_name_from_further_in() {
  CHECK(walk(R"C(let widest = 1
repeat 3 {
  set widest = max(widest, @i*10)
}
fill face x widest
)C") == "fill 120,200,300,400 1/0/0/1\n");
  CHECK(walk(R"C(let a = 1
set a = 5 if 0
fill face x a
)C") == "fill 101,200,300,400 1/0/0/1\n");
  CHECK(!load(R"C(set nothing = 1)C"));
}

void conditions_skip_what_they_guard() {
  CHECK(walk(R"C(fill face if @seats>0
fill ink if @seats==0
)C").starts_with("fill 100,200,300,400 0/0/0/0.5"));
  CHECK(walk(R"C(fill face if @seats>0
fill ink if @seats==0
)C", 0, 1).starts_with("fill 100,200,300,400 1/0/0/1"));
  // One that was skipped reads as zeros.
  CHECK(walk(R"C(fill face id a x 5 if 0
fill face x a.x+1
)C") == "fill 101,200,300,400 1/0/0/1\n");
}

void palettes_choose_colours_and_gradients() {
  CHECK(walk(R"C(fill face)C", 1) == "fill 100,200,300,400 0/1/0/1\n");
  CHECK(walk(R"C(fill ink)C", 1) == "fill 100,200,300,400 0/0/1/1\n");
  CHECK(walk(R"C(fill shade)C") == "down 100,200,300,400 200..600 2\n");
  CHECK(walk(R"C(fill shade down_to 20)C") == "down 100,200,300,400 200..220 2\n");
  CHECK(walk(R"C(fill shade radius 3)C") == "rounddown 100,200,300,400 3 200..600 2\n");
  CHECK(walk(R"C(fill shade across)C") == "across 100,200,300,400 2\n");
  CHECK(walk(R"C(fill face across)C") == "fill 100,200,300,400 1/0/0/1\n");  // a plain colour has no direction
  CHECK(!load(R"C(fill shade across radius 3)C").has_value());
  CHECK(!load(R"C(fill shade across 1)C").has_value());
  CHECK(walk(R"C(fill face radius 3)C") == "round 100,200,300,400 3 1/0/0/1\n");
  CHECK(walk(R"C(fill 99)C").empty());  // a number that is no entry draws nothing
  const auto pack = load(R"C(fill face)C");
  CHECK(pack->palette_of("one") == 0);
  CHECK(pack->palette_of("two") == 1);
  CHECK(!pack->palette_of("three").has_value());
}

void texts_come_from_three_places() {
  const std::string_view ok = locale::tr().ok;
  CHECK(walk(R"C(text @tr.ok style body color face h 16)C") == "text " + std::string(ok) + " 100,200,300,16 12/16/400 1/0/0/1\n");
  CHECK(walk(R"C(text @nickname style big color face)C") == "text Me 100,200,300,400 20/16/700 1/0/0/1\n");
  CHECK(walk(R"C(text "plain words" style body color face h 2)C").starts_with("text plain words"));
  CHECK(!load(R"C(text @tr.no_such_key style body color face)C"));
  CHECK(!load(R"C(text @nobody style body color face)C"));
}

void a_repeat_counts_with_i() {
  const std::string_view rows = R"C(repeat @seats {
  text @seat style body color face y @i*20 h 20 hit seat
}
)C";
  CHECK(walk(rows).empty());
  CHECK(walk(rows, 0, 2) ==
        "text A 100,200,300,20 12/20/400 1/0/0/1\nhit 100,200,300,20 100\n"
        "text B 100,220,300,20 12/20/400 1/0/0/1\nhit 100,220,300,20 101\n");
}

void a_hit_lights_what_is_inside_it() {
  const std::string_view row = R"C(repeat 2 {
  hit seat {
    fill @hot ? ink : face
  }
}
)C";
  const std::string lit = walk(row, 1, 0, 101);
  CHECK(lit == "fill 100,200,300,400 0/1/0/1\nhit 100,200,300,400 100\nfill 100,200,300,400 0/0/1/1\nhit 100,200,300,400 101\n");
}

void a_part_gets_its_box_label_and_arguments() {
  const std::string plain = walk(R"C(use button (0.25, 0.25, 50, 20) text @tr.ok hit join)C");
  const std::string_view ok = locale::tr().ok;
  // It snaps its box, and the hit keeps the box it was asked for.
  CHECK(plain == "round 100,200,50,20 3 1/0/0/1\ntext " + std::string(ok) + " 103,200,50,20 12/20/400 1/0/0/1\nfill 100,200,30,20 1/0/0/1\nhit 100.25,200.25,50,20 51\n");
  const std::string hot = walk(R"C(use button(off = 1) text "x" hit join)C", 0, 0, 51, 51);
  CHECK(hot == "round 100,200,300,400 3 0/0/0/0.5\ntext x 102,200,300,400 12/400/400 0/0/0/0.5\nfill 100,200,20,400 1/0/0/1\nhit 100,200,300,400 51\n");
}

void a_part_is_inside_its_own_names() {
  // Its let does not reach the screen, and a screen's does not reach it.
  CHECK(!load(R"C(use button
fill face x gap
)C"));
  CHECK(!load(R"C(use button(nothing = 1))C"));
  CHECK(!load(R"C(use nothing)C"));
  // Two uses keep to their own places.
  CHECK(walk(R"C(let keep = 7
use inner(amount = keep)
use inner
fill face x keep
)C") ==
        "fill 100,200,7,400 1/0/0/1\nfill 100,200,1,400 1/0/0/1\nfill 107,200,300,400 1/0/0/1\n");
}

void picture_and_field_go_to_the_screen() {
  CHECK(walk(R"C(picture me (1, 2, 3, 4))C") == "picture 31 101,202,3,4 1 0\n");
  CHECK(walk(R"C(picture me opacity 0.5 gray 1 w 3 h 4)C") == "picture 31 100,200,3,4 0.5 1\n");
  CHECK(walk(R"C(field code style body color face placeholder "type" placeholder_color ink caret ink selection ink)C") ==
        "field 41 100,200,300,400 type 1/0/0/1 0/0/0/0.5\n");
  CHECK(!load(R"C(picture nobody)C"));
  CHECK(!load(R"C(field nobody style body color face placeholder "t" placeholder_color ink caret ink selection ink)C"));
}

void a_column_stacks_what_it_draws() {
  CHECK(walk(R"C(column id c x 1 y 2 w 50 {
  fill face h 10
  fill face h 99 if 0
  fill face h 5 after 3
  fill face h 1
}
fill face y c.bottom w 1 h 1
)C") ==
        "fill 101,202,50,10 1/0/0/1\nfill 101,212,50,5 1/0/0/1\nfill 101,220,50,1 1/0/0/1\nfill 100,221,1,1 1/0/0/1\n");
  // A node can step back up by saying so, and a group inside starts again from the top.
  CHECK(walk(R"C(column w 5 {
  fill face h 10
  y @flow-1 h 5 {
    fill ink h 2
  }
}
)C") ==
        "fill 100,200,5,10 1/0/0/1\nfill 100,209,5,2 0/0/0/0.5\n");
}

void a_part_can_hold_a_list_and_say_how_tall_it_came_out() {
  const std::string out = walk(R"C(column w 100 {
  use box after 14 {
    use row text "ab"
    use row text "cd" if @seats
  }
  fill face h 1
}
)C", 0, 1);
  CHECK(out == "text ab 110,214,20,20 12/20/400 1/0/0/1\ntext cd 110,234,20,20 12/20/400 1/0/0/1\nframe 100,200,100,60 0/0/0/0.5\nfill 100,274,100,1 1/0/0/1\n");
  // Without the second row it is that much shorter.
  const std::string less = walk(R"C(column w 100 {
  use box after 14 {
    use row text "ab"
    use row text "cd" if @seats
  }
  fill face h 1
}
)C");
  CHECK(less == "text ab 110,214,20,20 12/20/400 1/0/0/1\nframe 100,200,100,40 0/0/0/0.5\nfill 100,254,100,1 1/0/0/1\n");
}

void a_part_sizes_itself_where_none_is_given() {
  CHECK(walk(R"C(use row text "abc")C") == "text abc 100,200,30,20 12/20/400 1/0/0/1\n");
  CHECK(walk(R"C(use row text "abc" w 7)C") == "text abc 100,200,7,20 12/20/400 1/0/0/1\n");
  CHECK(walk(R"C(use row text "abc" right 100)C") == "text abc 170,200,30,20 12/20/400 1/0/0/1\n");
}

void right_and_bottom_measure_from_the_far_edge() {
  CHECK(walk(R"C(fill face right @W-1 bottom @H-2 w 10 h 20)C") == "fill 389,578,10,20 1/0/0/1\n");
  CHECK(!load(R"C(fill face right 1 x 1)C"));
  CHECK(!load(R"C(fill face bottom 1 y 1)C"));
}

void hits_can_be_gated_and_counted_otherwise() {
  CHECK(walk(R"C(hit join w 1 h 1 live 0 {
  fill @hot ? ink : face
}
)C", 0, 0, 51).starts_with("fill 100,200,1,1 1/0/0/1\nhit"));
  CHECK(walk(R"C(hit join w 1 h 1 hit_if 0 {
  fill face
}
)C") == "fill 100,200,1,1 1/0/0/1\n");
  CHECK(walk(R"C(hit seat hit_index 7 w 1 h 1 {
  fill face
}
)C").ends_with("hit 100,200,1,1 107\n"));
}

void numbers_are_read_for_the_round_of_a_repeat() {
  CHECK(walk(R"C(repeat 2 {
  fill face x @twice w 1 h 1
}
)C") == "fill 100,200,1,1 1/0/0/1\nfill 102,200,1,1 1/0/0/1\n");
}

void lines_paths_and_measures() {
  CHECK(walk(R"C(line face (1, 2) to (3, 4) width 2)C") == "line 101,202 103,204 1/0/0/1 2\n");
  CHECK(walk(R"C(path "M0 0H1" (1, 2) color face)C") == "path M0 0H1 101,202 x1 1/0/0/1 0 round\n");
  CHECK(walk(R"C(path "M0 0H1" color face scale 0.5 width 2 caps flat)C") == "path M0 0H1 100,200 x0.5 1/0/0/1 2 flat\n");
  CHECK(!load(R"C(path M0 color face caps square)C"));
  // 40 wide at 10 a letter in 15 makes three lines of 16.
  CHECK(walk(R"C(fill face w 1 h texth(big, "abcd", 15))C") == "fill 100,200,1,48 1/0/0/1\n");
  CHECK(walk(R"C(fill face x snap(1.4) w 1 h 1)C") == "fill 101,200,1,1 1/0/0/1\n");
}

void slots_reports_and_notes_go_to_the_screen() {
  CHECK(walk(R"C(slot list numbers (3) styles (body) colors (face))C") == "slot 61 100,200,300,400 1/1/1 3\n");
  CHECK(walk(R"C(report height = @W+1)C") == "report 71 301\n");
  CHECK(walk(R"C(fill face note where w 1 h 1)C") == "note 81/0 100,200,1,1\nfill 100,200,1,1 1/0/0/1\n");
  CHECK(!load(R"C(slot nothing)C"));
  CHECK(!load(R"C(report nothing = 1)C"));
  CHECK(!load(R"C(fill face note nothing)C"));
}

void a_part_can_be_drawn_alone() {
  const auto pack = load(R"C(fill face)C");
  Log log;
  TestSource source;
  source.log = &log;
  const Part* part = pack->part("button");
  CHECK(part != nullptr);
  const std::pair<std::string_view, float> off{"off", 1};
  draw_part(*pack, *part, 0, {1, 2, 30, 20}, log, source, {.label = "x", .hot = true, .pressed = false, .args = std::span(&off, 1)});
  CHECK(log.out == "round 1,2,30,20 3 1/0/0/1\ntext x 3,2,30,20 12/20/400 0/0/0/0.5\nfill 1,2,20,20 1/0/0/1\n");
}

void a_clip_goes_around_a_group() {
  CHECK(walk(R"C(clip w 5 h 6 {
  fill face
}
)C") == "clip 100,200,5,6\nfill 100,200,5,6 1/0/0/1\nunclip\n");
}

void a_bad_node_is_refused() {
  CHECK(!load(R"C(1)C"));
  CHECK(!load(R"C(fill)C"));
  CHECK(!load(R"C(fill face frame ink)C"));
  CHECK(!load(R"C(fill face colour ink)C"));
  CHECK(!load(R"C(frame face radius 1)C"));
  CHECK(!load(R"C(fill nobody)C"));
  CHECK(!load(R"C(fill face hit nobody)C"));
  CHECK(!load(R"C(stroke face)C"));
  CHECK(!load(R"C(text "x" style nobody color face)C"));
  CHECK(!load(R"C(repeat 2)C"));
  CHECK(!load(R"C(fill face x [1])C"));
  CHECK(walk(R"C(fill face x 1 alpha 0.5)C") == "fill 101,200,300,400 1/0/0/0.5\n");
  CHECK(walk(R"C(fill face x 1 // a comment)C") == "fill 101,200,300,400 1/0/0/1\n");
}

std::expected<Pack, std::string> load_with(std::string_view from, std::string_view to) {
  std::string pack = test_pack;
  const std::size_t at = pack.find(from);
  CHECK(at != std::string::npos);
  pack.replace(at, from.size(), to);
  return load_pack(files(pack, std::string(R"C(fill face)C")), std::span(&test_schema, 1));
}

void a_bad_pack_is_refused() {
  CHECK(load_with("format 1", "format 1").has_value());
  CHECK(!load_with("format 1", "format 2"));
  CHECK(!load_with(" format 1", ""));
  CHECK(!load_with("pack test format 1", ""));
  CHECK(!load_with("#ff0000", "red"));
  CHECK(!load_with("#ff0000", "#ff00"));
  CHECK(!load_with("face #00ff00", "other #00ff00"));
  CHECK(load_with("ink #00000080", "ink mix(face, face, 50%)").has_value());
  CHECK(load_with("ink #00000080", "ink alpha(face, 0.5)").has_value());
  CHECK(!load_with("ink #00000080", "ink mix(nobody, face, 50%)"));
  CHECK(!load_with("ink #00000080", "ink alpha(shade, 0.5)"));
  CHECK(!load_with("face #00ff00", "face stops(0: #000000, 1: #000000)"));  // a colour in the first
  CHECK(!load_with("size 20 ", ""));
  CHECK(!load_with("align center", "align middle"));
  CHECK(!load_pack([](std::string_view) { return std::optional<std::string>{}; }, std::span(&test_schema, 1)));
  CHECK(!load_pack(files("not a pack", ""), std::span(&test_schema, 1)));
  CHECK(!load_pack(files(test_pack, "fill face {"), std::span(&test_schema, 1)));
  CHECK(!load_pack(files(test_pack, "}"), std::span(&test_schema, 1)));
}

void an_error_says_where() {
  const auto pack = load(R"C(fill nobody)C");
  CHECK(!pack && pack.error().find("screens/home.cskin") != std::string::npos && pack.error().find("nobody") != std::string::npos);
  const auto bad = load_with("#ff0000", "red");
  CHECK(!bad && bad.error().find("pack.cskin:5:8") != std::string::npos);  // face red
}

void a_mistake_in_the_language_says_where() {
  const auto broken = load_pack(files(test_pack, "// a comment\nfill face\nfill face radius\n"), std::span(&test_schema, 1));
  CHECK(!broken && broken.error().find("screens/home.cskin:3:17") != std::string::npos);
  std::string pack = test_pack;
  pack.replace(pack.find("face #ff0000"), 12, "face #ff00");
  const auto bad = load_pack(files(pack, "fill face"), std::span(&test_schema, 1));
  CHECK(!bad && bad.error().find("pack.cskin:5:") != std::string::npos);
  // What only the loader finds is said at the line of its node.
  const auto named = load("fill face\ngroup {\n  fill face\n  fill nobody\n}\nfill face");
  CHECK(!named && named.error().starts_with("screens/home.cskin:4: ") && named.error().find("nobody") != std::string::npos);
  std::string in_part = test_pack;
  in_part.replace(in_part.find("  fill face w amount"), 20, "  fill face w nobody");
  const auto part = load_pack(files(in_part, "fill face"), std::span(&test_schema, 1));
  CHECK(!part && part.error().starts_with("pack.cskin:") && part.error().find(": part \"inner\"") != std::string::npos);
}

void a_screen_without_a_file_is_left_out() {
  const auto pack = load_pack(files(test_pack, ""), std::span(&test_schema, 1));
  CHECK(pack.has_value());
  CHECK(pack->screen("home") == nullptr);
}

void sizes_are_limited() {
  // A file too large.
  CHECK(!load_pack(files(std::string(max_file_bytes + 1, ' ') + test_pack, ""), std::span(&test_schema, 1)));
  // Nodes too deep.
  std::string deep = R"C(fill face)C";
  for (int i = 0; i < max_node_depth + 1; ++i) deep = "group {\n" + deep + "\n}";
  CHECK(!load(deep));
  // Too many names.
  std::string names;
  for (int i = 0; i <= max_frame_slots; ++i) names += "let n" + std::to_string(i) + " = 1\n";
  CHECK(!load(names));
}

void a_walk_ends_however_parts_multiply() {
  // Each part uses the one before it twice, which makes 2^40 nodes.
  std::string pack = "pack t format 1\npalette one {\n  face #000000\n}\npart p0 {\n  fill face\n}\n";
  for (int i = 1; i <= 40; ++i) {
    const std::string before = "p" + std::to_string(i - 1);
    pack += "part p" + std::to_string(i) + " {\n  use " + before + "\n  use " + before + "\n}\n";
  }
  const auto loaded = load_pack(files(pack, std::string(R"C(use p40)C")), std::span(&test_schema, 1));
  CHECK(loaded.has_value());
  if (!loaded) return;
  class Counter final : public Canvas {
   public:
    int fills = 0;
    Box snap(Box b) override { return b; }
    float line() override { return 1; }
    float font_size() override { return 12; }
    float snap(float v) override { return v; }
    float text_width(std::string_view, const TextLook&, float) override { return 0; }
    float text_height(std::string_view, const TextLook&, float) override { return 0; }
    void fill(Box, Color) override { ++fills; }
    void fill_down(Box, float, float, std::span<const GradientStop>) override {}
    void fill_round(Box, float, Color) override {}
    void fill_round_down(Box, float, float, float, std::span<const GradientStop>) override {}
    void frame(Box, Color) override {}
    void stroke_round(Box, float, Color, float) override {}
    void line(float, float, float, float, Color, float) override {}
    void path(std::string_view, float, float, float, Color, float, bool, bool) override {}
    void text(std::string_view, Box, const TextLook&, Color) override {}
    void push_clip(Box) override {}
    void pop_clip() override {}
    void hit(Box, int) override {}
    bool hot(int) override { return false; }
    bool pressed(int) override { return false; }
  } counter;
  TestSource source;
  draw(*loaded, *loaded->screen("home"), 0, {0, 0, 10, 10}, counter, source);
  CHECK(counter.fills <= max_walk);
}

}  // namespace

void a_group_can_count_from_its_corner_and_go_down() {
  CHECK(walk(R"C((1, 2) scale 2 origin {
  fill face (3, 4, 5, 6)
}
)C") == "scale at 101,202 x2\nfill 3,4,5,6 1/0/0/1\nunscale\n");
  CHECK(walk(R"C(dy 7 {
  fill face w 5 h 6
}
)C") == "down 7\nfill 100,200,5,6 1/0/0/1\nunscale\n");
  CHECK(!load(R"C(origin)C"));
}

void a_clip_can_be_soft_and_a_path_stretched() {
  CHECK(walk(R"C(clip soft_clip)C") == "softclip 100,200,300,400\nunclip\n");
  CHECK(walk(R"C(path "M0 0" (1, 2) color face scale -2 scale_y 3)C") == "stretched M0 0 101,202 x-2 y3 1/0/0/1\n");
  CHECK(!load(R"C(path "M0 0" color face scale_y 3 width 1)C"));
}

void a_transition_is_asked_of_the_canvas() {
  CHECK(walk(R"C(let v = ease(1, 150ms) ms_off 500ms first jump curve linear
fill face w v*10
)C") ==
        "transition on 150/500 jump linear\nfill 100,200,5,400 1/0/0/1\n");
  CHECK(walk(R"C(let v = ease(0, 150ms)
fill face w v*10
)C") == "transition off 150/150\nfill 100,200,5,400 1/0/0/1\n");
  // A transition reads what is hot where it is.
  CHECK(walk(R"C(hit join {
  let v = ease(@hot, 1ms)
}
)C", 0, 0, 51) == "transition on 1/1\nhit 100,200,300,400 51\n");
  CHECK(walk(R"C(let v = ease(1, 1ms) again 3)C") == "transition on 1/1 again 3\n");
  CHECK(!load(R"C(let v = 1 again 3)C"));
  CHECK(!load(R"C(let v = ease(1) ms_off 1ms)C"));
  CHECK(!load(R"C(let v = ease(1, 1ms) value 1)C"));
  CHECK(!load(R"C(let v = 1 ms 1)C"));
  CHECK(!load(R"C(let v = ease(1, 1ms) curve bounce)C"));
  CHECK(!load(R"C(let v = ease(1, 1ms) first late)C"));
}

void a_transition_can_belong_to_a_number() {
  const auto keys_of = [](std::string_view nodes) {
    const auto pack = load(nodes);
    CHECK(pack.has_value());
    Log log;
    TestSource source;
    source.log = &log;
    source.seats = 2;
    if (pack) draw(*pack, *pack->screen("home"), 0, {0, 0, 100, 100}, log, source);
    return log.keys;
  };
  // The same number, whichever round it is in; another number is another transition.
  const auto same = keys_of(R"C(repeat @seats {
  let v = ease(1, 1ms) key 7
}
)C");
  CHECK(same.size() == 2 && same[0] == same[1]);
  const auto apart = keys_of(R"C(repeat @seats {
  let v = ease(1, 1ms) key @i
}
)C");
  CHECK(apart.size() == 2 && apart[0] != apart[1]);
  CHECK(!load(R"C(let v = 1 key 3)C"));
}

void transitions_are_told_apart_where_they_are() {
  const auto keys_of = [](std::string_view nodes, int rounds) {
    const auto pack = load(nodes);
    CHECK(pack.has_value());
    Log log;
    TestSource source;
    source.log = &log;
    source.seats = rounds;
    if (pack) draw(*pack, *pack->screen("home"), 0, {0, 0, 100, 100}, log, source);
    return log.keys;
  };
  // Each round of a repeat, and each of two nodes.
  const auto rounds = keys_of(R"C(repeat @seats {
  let v = ease(1, 1ms)
}
)C", 2);
  CHECK(rounds.size() == 2 && rounds[0] != rounds[1]);
  const auto two = keys_of(R"C(let v = ease(1, 1ms)
let u2 = ease(1, 1ms)
)C", 0);
  CHECK(two.size() == 2 && two[0] != two[1]);
  // And the same frame gives the same keys.
  CHECK(keys_of(R"C(let v = ease(1, 1ms)
let u2 = ease(1, 1ms)
)C", 0) == two);
}

void colours_are_worked_out_in_expressions() {
  CHECK(walk(R"C(fill mix(face, ink, 100%))C") == "fill 100,200,300,400 1/0/0/1\n");
  CHECK(walk(R"C(fill alpha(face, 0.25))C") == "fill 100,200,300,400 1/0/0/0.25\n");
  CHECK(walk(R"C(fill alpha(mix(face, face, 50%), 0.5) radius 2)C") == "round 100,200,300,400 2 1/0/0/0.5\n");
  CHECK(walk(R"C(let c = mix(face, face, 50%)
frame c
)C") == "frame 100,200,300,400 1/0/0/1\n");
  CHECK(!load(R"C(fill mix(face, ink))C"));
}

void dark_says_which_palette_is_a_dark_variant() {
  const std::string themed = R"C(pack test format 1

theme one names en "One" dark two

default_theme one


palette one {
  face #ff0000
}

palette two {
  face #00ff00
}
)C";
  const auto pack = load_pack(files(themed, std::string(R"C(fill face w @dark ? 1 : 2)C")), std::span(&test_schema, 1));
  CHECK(pack.has_value());
  if (!pack) return;
  CHECK(walk(*pack, 0, 0, -1, -1) == "fill 100,200,2,400 1/0/0/1\n");
  CHECK(walk(*pack, 1, 0, -1, -1) == "fill 100,200,1,400 0/1/0/1\n");
}

void ellipses_shadows_and_paths_of_other_kinds() {
  CHECK(walk(R"C(fill face ellipse w 10 h 20)C") == "ellipse 100,200,10,20 1/0/0/1\n");
  CHECK(walk(R"C(stroke face ellipse w 10 h 20 width 2)C") == "ring 100,200,10,20 1/0/0/1 2\n");
  CHECK(!load(R"C(stroke face w 10 width 2)C"));
  CHECK(walk(R"C(shadow face radius 14 blur 16 dy 4 w 10 h 20)C") == "shadow 100,200,10,20 14 16 4 1/0/0/1\n");
  CHECK(walk(R"C(path "M0 0" (1, 2) color shade scale 3 from_y 8 to_y 9)C") == "pathdown M0 0 101,202 x3 8..9 2\n");
  CHECK(walk(R"C(path "M0 0" (1, 2) color face blur 6 dy 7)C") == "pathshadow M0 0 101,202 x1 6 7 1/0/0/1\n");
  CHECK(!load(R"C(path "M0 0" color face blur 6 from_y 1 to_y 2)C"));
}

void a_group_can_grow_fade_and_be_seen_through() {
  CHECK(walk(R"C((10, 20, 40, 60) scale 1.5 {
  fill face w 1
}
)C") ==
        "scale 130,250 x1.5\nfill 110,220,1,60 1/0/0/1\nunscale\n");
  CHECK(walk(R"C((10, 20, 40, 60) about (1, 2) scale 2)C") == "scale 101,202 x2\nunscale\n");
  CHECK(walk(R"C(w 40 h 60 opacity 0.5 {
  fill face w 1
}
)C") == "opacity 100,200,40,60 0.5\nfill 100,200,1,60 1/0/0/1\nunlayer\n");
  CHECK(walk(R"C(w 40 h 60 opacity 1)C") == "opacity 100,200,40,60 1\n");
  CHECK(walk(R"C(w 40 h 60 fade_top 3 fade_bottom 0)C") == "fade 100,200,40,60 3 0\nunlayer\n");
  CHECK(walk(R"C(w 40 h 60 fade_top 0)C") == "fade 100,200,40,60 0 0\n");
}

void a_hit_can_take_the_shape_of_a_path() {
  CHECK(walk(R"C(hit join hit_path_at (5, 6) hit_path "M1 1" x 10 w 20 h 30)C") == "hitshape 110,200,20,30 51 M1 1 105,206\n");
  CHECK(!load(R"C(hit_path "M1 1")C"));
}

void test_cursors_are_all_there_or_refused() {
  const auto with = [](std::string cursors) {
    const std::map<std::string, std::string> files = {{"pack.cskin", "pack t format 1\npalette a {\n  ink #000000\n}\npart p {\n  fill ink\n}\ncursors " + std::move(cursors)}};
    return load_pack([files](std::string_view path) -> std::optional<std::string> {
      const auto found = files.find(std::string(path));
      if (found == files.end()) return std::nullopt;
      return found->second;
    }, {});
  };
  std::string all = "{\n";
  for (const std::string_view name : cursor_names) all += "  " + std::string(name) + " part p hot (2, 3)\n";
  all += "}";
  const auto good = with(all);
  CHECK(good.has_value());
  if (good) {
    CHECK(good->cursors.has_value());
    CHECK((*good->cursors)[static_cast<std::size_t>(CursorKind::IBeam)].hot_y == 3);
    CHECK(!good->cursors_hard);
  }
  CHECK(!with("{\n  arrow part p hot (2, 3)\n}").has_value());
  std::string hard = "hard " + all;
  const auto with_hard_edges = with(hard);
  CHECK(with_hard_edges.has_value() && with_hard_edges->cursors_hard);
  hard.replace(0, 4, "1");
  CHECK(!with(hard).has_value());
  std::string no_part = all;
  no_part.replace(no_part.find("part p "), 7, "part nobody ");
  CHECK(!with(no_part).has_value());
  std::string outside = all;
  outside.replace(outside.find("(2, 3)"), 6, "(2, 33)");
  CHECK(!with(outside).has_value());
}

using Files = std::map<std::string, std::string, std::less<>>;

ReadFile from(Files files) {
  return [files = std::move(files)](std::string_view path) -> std::optional<std::string> {
    const auto found = files.find(path);
    if (found == files.end()) return std::nullopt;
    return found->second;
  };
}

// Packs by id, for what extends them.
LoadOptions parents(std::map<std::string, Files, std::less<>> packs, std::string overlay = {}) {
  LoadOptions options;
  options.overlay = std::move(overlay);
  options.parent = [packs = std::move(packs)](std::string_view id) -> std::optional<ReadFile> {
    const auto found = packs.find(id);
    if (found == packs.end()) return std::nullopt;
    return from(found->second);
  };
  return options;
}

const char* const parent_pack = R"C(pack base format 1

constants {
  gap 1
  only_base 7
}


palette one {
  face #ff0000
  ink #000000
}

palette one_dark extends one {
  face #110000
}

theme one names ko "하나" en "One" dark one_dark

default_theme one

style body size @font_size line_height @h


part a {
  fill face
}

part b {
  use a
}
)C";

void a_child_replaces_by_name_and_keeps_the_rest() {
  const Files child = {{"pack.cskin", R"C(pack child format 1 extends base

constants {
  gap 2
}


palette two extends one {
  ink #00ff00
}

theme two names ko "둘"


part a {
  use new_one
}

part new_one {
  fill ink
}
)C"}};
  const Files base = {{"pack.cskin", parent_pack}, {"screens/home.cskin", std::string(R"C(use b)C")}};
  const auto pack = load_pack(from(child), std::span(&test_schema, 1), parents({{"base", base}}));
  CHECK(pack.has_value());
  if (!pack) return;
  CHECK(pack->id == "child");
  CHECK(pack->constant("gap") == 2 && pack->constant("only_base") == 7);
  CHECK(pack->screen("home") != nullptr);  // from the parent
  CHECK(pack->themes.size() == 2 && pack->default_theme == "one");
  CHECK(pack->palette_of("one").has_value() && pack->palette_of("one_dark").has_value() && pack->palette_of("two").has_value());
  const auto dark = pack->palette_of("one_dark");
  const Paint* face = pack->paint_named(*dark, "face");
  const Paint* ink = pack->paint_named(*dark, "ink");
  CHECK(face && face->color.r < 0.1f);   // its own
  CHECK(ink && ink->color.g == 0);       // from what it extends
  const Paint* two = pack->paint_named(*pack->palette_of("two"), "ink");
  CHECK(two && two->color.g == 1);
  // A part replaced in place can use one that came after it.
  const auto used = std::ranges::find(pack->parts, "new_one", &Part::name);
  const auto user = std::ranges::find(pack->parts, "a", &Part::name);
  CHECK(used < user);
  CHECK(pack->theme_of_palette("one_dark") && pack->theme_of_palette("one_dark")->id == "one");
}

void a_child_takes_a_screen_of_its_own() {
  const Files child = {{"pack.cskin", R"C(pack child format 1 extends base)C"}, {"screens/home.cskin", std::string(R"C(fill ink)C")}};
  const Files base = {{"pack.cskin", parent_pack}, {"screens/home.cskin", std::string(R"C(use b)C")}};
  const auto pack = load_pack(from(child), std::span(&test_schema, 1), parents({{"base", base}}));
  CHECK(pack.has_value());
  if (!pack) return;
  const Screen* home = pack->screen("home");
  CHECK(home && pack->nodes[home->first].kind == NodeKind::Fill);
}

void extending_is_refused_where_it_cannot_work() {
  const auto pack_named = [](std::string id, std::string parent) {
    return Files{{"pack.cskin", "pack " + id + " format 1 extends " + parent}};
  };
  const Files base = {{"pack.cskin", parent_pack}};
  CHECK(!load_pack(from(pack_named("child", "base")), {}));  // nothing finds parents
  CHECK(!load_pack(from(pack_named("child", "nobody")), {}, parents({{"base", base}})));
  // Around in a circle.
  CHECK(!load_pack(from(pack_named("a", "b")), {}, parents({{"b", pack_named("b", "a")}, {"a", pack_named("a", "b")}})));
  // Three between it and the parent is the limit of four levels.
  const auto chain = parents({{"l1", pack_named("l1", "l2")}, {"l2", pack_named("l2", "base")}, {"base", base}});
  CHECK(load_pack(from(pack_named("top", "l1")), {}, chain).has_value());
  const auto longer = parents({{"l0", pack_named("l0", "l1")}, {"l1", pack_named("l1", "l2")}, {"l2", pack_named("l2", "base")}, {"base", base}});
  CHECK(!load_pack(from(pack_named("top", "l0")), {}, longer));
  // Another format.
  const Files other = {{"pack.cskin", R"C(pack base format 99)C"}};
  CHECK(!load_pack(from(pack_named("child", "base")), {}, parents({{"base", other}})));
  // Parts that use each other.
  const Files circle = {{"pack.cskin", R"C(pack c format 1 extends base


part a {
  use b
}
)C"}};
  CHECK(!load_pack(from(circle), {}, parents({{"base", base}})));
  // A Palette extending what is not there, and a Theme naming a Palette that is not there.
  CHECK(!load_pack(from({{"pack.cskin", R"C(pack c format 1 extends base


palette x extends nobody {
}
)C"}}), {}, parents({{"base", base}})));
  CHECK(!load_pack(from({{"pack.cskin", R"C(pack c format 1 extends base

theme x names ko "x"
)C"}}), {}, parents({{"base", base}})));
  CHECK(!load_pack(from({{"pack.cskin", R"C(pack c format 1 extends base

default_theme nobody
)C"}}), {}, parents({{"base", base}})));
}

void an_overlay_lies_on_top_for_its_own_themes() {
  const Files files = {
      {"pack.cskin", R"C(pack base format 1

constants {
  gap 1
}


palette plain {
  face #ff0000
}

theme plain names ko "보통"
theme fancy names ko "멋" overlay shiny dark fancy_dark

default_theme plain



part a {
  fill face
}
)C"},
      {"screens/home.cskin", std::string(R"C(use a)C")},
      {"overlays/shiny/pack.cskin", R"C(pack shiny format 1

constants {
  gap 2
}


palette fancy {
  face #00ff00
  extra #0000ff
}

palette fancy_dark extends fancy {
  face #001100
}


part a {
  fill extra
}
)C"},
  };
  const auto plain = load_pack(from(files), std::span(&test_schema, 1));
  CHECK(plain.has_value());
  if (plain) {
    CHECK(plain->constant("gap") == 1 && plain->overlay.empty());
    CHECK(plain->palette_of("plain").has_value() && !plain->palette_of("fancy").has_value());
    CHECK(plain->theme_of_palette("fancy_dark") && plain->theme_of_palette("fancy_dark")->overlay == "shiny");  // known either way
  }
  LoadOptions shiny;
  shiny.overlay = "shiny";
  const auto fancy = load_pack(from(files), std::span(&test_schema, 1), shiny);
  CHECK(fancy.has_value());
  if (fancy) {
    CHECK(fancy->constant("gap") == 2 && fancy->overlay == "shiny" && fancy->id == "base");
    CHECK(!fancy->palette_of("plain").has_value());  // it has not the entries the other has
    CHECK(fancy->palette_of("fancy").has_value() && fancy->palette_of("fancy_dark").has_value());
    CHECK(fancy->screen("home") != nullptr);
  }
  LoadOptions nobody;
  nobody.overlay = "nobody";
  CHECK(!load_pack(from(files), std::span(&test_schema, 1), nobody));
}

int main() {
  a_child_replaces_by_name_and_keeps_the_rest();
  a_child_takes_a_screen_of_its_own();
  extending_is_refused_where_it_cannot_work();
  an_overlay_lies_on_top_for_its_own_themes();
  boxes_are_measured_from_the_box_around_them();
  siblings_read_each_others_boxes();
  a_let_is_read_further_on_and_inside();
  a_set_changes_a_name_from_further_in();
  a_text_can_set_its_size();
  a_part_takes_its_hit_and_note_from_the_screen();
  conditions_skip_what_they_guard();
  palettes_choose_colours_and_gradients();
  texts_come_from_three_places();
  a_repeat_counts_with_i();
  a_hit_lights_what_is_inside_it();
  a_part_gets_its_box_label_and_arguments();
  a_part_is_inside_its_own_names();
  picture_and_field_go_to_the_screen();
  a_column_stacks_what_it_draws();
  a_part_can_hold_a_list_and_say_how_tall_it_came_out();
  a_part_sizes_itself_where_none_is_given();
  right_and_bottom_measure_from_the_far_edge();
  hits_can_be_gated_and_counted_otherwise();
  numbers_are_read_for_the_round_of_a_repeat();
  lines_paths_and_measures();
  slots_reports_and_notes_go_to_the_screen();
  a_part_can_be_drawn_alone();
  a_clip_goes_around_a_group();
  a_bad_node_is_refused();
  a_bad_pack_is_refused();
  an_error_says_where();
  a_mistake_in_the_language_says_where();
  a_screen_without_a_file_is_left_out();
  sizes_are_limited();
  a_walk_ends_however_parts_multiply();
  test_cursors_are_all_there_or_refused();
  a_group_can_count_from_its_corner_and_go_down();
  a_clip_can_be_soft_and_a_path_stretched();
  a_transition_is_asked_of_the_canvas();
  a_transition_can_belong_to_a_number();
  transitions_are_told_apart_where_they_are();
  colours_are_worked_out_in_expressions();
  dark_says_which_palette_is_a_dark_variant();
  ellipses_shadows_and_paths_of_other_kinds();
  a_group_can_grow_fade_and_be_seen_through();
  a_hit_can_take_the_shape_of_a_path();
  return crpp_test_result();
}
