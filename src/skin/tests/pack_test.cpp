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

const char* const test_pack = R"J({
  "format": 1,
  "id": "test",
  "palettes": {
    "one": {"face": "#ff0000", "ink": "#00000080", "shade": [[0, "#000000"], [1, "#ffffff"]]},
    "two": {"shade": [[0, "#111111"], [1, "#222222"]], "ink": "#0000ff", "face": "#00ff00"}
  },
  "styles": {
    "body": {"size": "font_size", "line_height": "h", "ellipsis": true, "exact": true},
    "big": {"size": 20, "line_height": "font_size+4", "weight": 700, "align": "center", "wrap": true}
  },
  "parts": {
    "by": {
      "args": {"own": -1, "where": -1},
      "draw": [{"hit_by": "own", "note_by": "where", "group": [{"fill": "hot ? $ink : $face"}]}]
    },
    "inner": {
      "args": {"amount": 1},
      "draw": [{"fill": "$face", "w": "amount"}]
    },
    "row": {
      "h": 20,
      "w": "textw('body', '@label')",
      "draw": [{"text": "@label", "style": "body", "color": "$face"}]
    },
    "box": {
      "height": "rows.bottom+6",
      "draw": [
        {"id": "rows", "x": 10, "y": 14, "w": "W-20", "column": [{"content": true}]},
        {"frame": "$ink", "h": "rows.bottom+6"}
      ]
    },
    "button": {
      "snap": true,
      "args": {"off": 0},
      "draw": [
        {"let": "gap", "value": "off ? 2 : 3"},
        {"fill": "pressed ? $ink : $face", "radius": 3},
        {"text": "@label", "style": "body", "color": "hot ? $ink : $face", "x": "gap"},
        {"use": "inner", "args": {"amount": "gap*10"}}
      ]
    }
  }
})J";

std::string fill_list(std::string_view nodes) { return "{\"draw\":[" + std::string(nodes) + "]}"; }

ReadFile files(std::string pack, std::string home) {
  return [pack = std::move(pack), home = std::move(home)](std::string_view path) -> std::optional<std::string> {
    if (path == "pack.jsonc") return pack;
    if (path == "screens/home.jsonc" && !home.empty()) return home;
    return std::nullopt;
  };
}

std::expected<Pack, std::string> load(std::string_view nodes) {
  return load_pack(files(test_pack, fill_list(nodes)), std::span(&test_schema, 1));
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
  void pop_clip() override { add("unclip"); }
  void hit(Box b, int id) override { add("hit " + str(b) + " " + std::to_string(id)); }
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
  CHECK(walk(R"({"fill":"$face"})") == "fill 100,200,300,400 1/0/0/1\n");
  CHECK(walk(R"({"fill":"$face","x":10,"y":"H-5","w":"W/2","h":5})") == "fill 110,595,150,5 1/0/0/1\n");
  CHECK(walk(R"({"group":[{"fill":"$face","x":1,"y":2,"w":"W-2","h":"H-4"}],"x":10,"y":20,"w":100,"h":50})") == "fill 111,222,98,46 1/0/0/1\n");
  CHECK(walk(R"({"fill":"$face","y":10,"h":"y*2"})") == "fill 100,210,300,20 1/0/0/1\n");
}

void siblings_read_each_others_boxes() {
  CHECK(walk(R"({"fill":"$face","id":"a","x":5,"y":6,"w":7,"h":8},{"fill":"$face","x":"a.right","y":"a.bottom","w":"a.w","h":"a.h"})") ==
        "fill 105,206,7,8 1/0/0/1\nfill 112,214,7,8 1/0/0/1\n");
  // Not a node further on, nor one in another list.
  CHECK(!load(R"({"fill":"$face","x":"a.x"},{"fill":"$face","id":"a"})"));
  CHECK(!load(R"({"group":[{"fill":"$face","id":"a"}]},{"fill":"$face","x":"a.x"})"));
  CHECK(!load(R"({"fill":"$face","id":"a"},{"group":[{"fill":"$face","x":"a.x"}]})"));
}

void a_let_is_read_further_on_and_inside() {
  CHECK(walk(R"({"let":"gap","value":"W/10"},{"group":[{"fill":"$face","x":"gap"}]})") == "fill 130,200,300,400 1/0/0/1\n");
  CHECK(!load(R"({"fill":"$face","x":"gap"},{"let":"gap","value":1})"));
  CHECK(!load(R"({"let":"gap","value":1},{"let":"gap","value":2})"));
  CHECK(!load(R"({"let":"W","value":1})"));
}

void a_part_takes_its_hit_and_note_from_the_screen() {
  CHECK(walk(R"J({"use":"by","args":{"own":"hit('join')"}})J", 0, 0, 51) == "fill 100,200,300,400 0/0/0/0.5\nhit 100,200,300,400 51\n");
  // Less than 0 is none, and leaves what is lit from outside alone.
  CHECK(walk(R"J({"use":"by"})J") == "fill 100,200,300,400 1/0/0/1\n");
  CHECK(walk(R"J({"use":"by","hit":"join"})J", 0, 0, 51) == "fill 100,200,300,400 0/0/0/0.5\nhit 100,200,300,400 51\n");
  CHECK(walk(R"J({"use":"by","args":{"where":"note('where')"}})J").starts_with("note 81"));
  CHECK(!load(R"J({"fill":"$face","x":"hit('nothing')"})J"));
  CHECK(!load(R"J({"fill":"$face","x":"note('nothing')"})J"));
}

void a_text_can_set_its_size() {
  CHECK(walk(R"J({"text":"x","style":"body","color":"$face","h":10,"size":"h+5","line_height":"h+9"})J") == "text x 100,200,300,10 15/19/400 1/0/0/1\n");
}

void a_set_changes_a_name_from_further_in() {
  CHECK(walk(R"J({"let":"widest","value":1},{"repeat":3,"draw":[{"set":"widest","value":"max(widest, i*10)"}]},{"fill":"$face","x":"widest"})J") == "fill 120,200,300,400 1/0/0/1\n");
  CHECK(walk(R"({"let":"a","value":1},{"if":"0","set":"a","value":5},{"fill":"$face","x":"a"})") == "fill 101,200,300,400 1/0/0/1\n");
  CHECK(!load(R"({"set":"nothing","value":1})"));
}

void conditions_skip_what_they_guard() {
  CHECK(walk(R"({"if":"@seats>0","fill":"$face"},{"if":"@seats==0","fill":"$ink"})").starts_with("fill 100,200,300,400 0/0/0/0.5"));
  CHECK(walk(R"({"if":"@seats>0","fill":"$face"},{"if":"@seats==0","fill":"$ink"})", 0, 1).starts_with("fill 100,200,300,400 1/0/0/1"));
  // One that was skipped reads as zeros.
  CHECK(walk(R"({"if":0,"fill":"$face","id":"a","x":5},{"fill":"$face","x":"a.x+1"})") == "fill 101,200,300,400 1/0/0/1\n");
}

void palettes_choose_colours_and_gradients() {
  CHECK(walk(R"({"fill":"$face"})", 1) == "fill 100,200,300,400 0/1/0/1\n");
  CHECK(walk(R"({"fill":"$ink"})", 1) == "fill 100,200,300,400 0/0/1/1\n");
  CHECK(walk(R"({"fill":"$shade"})") == "down 100,200,300,400 200..600 2\n");
  CHECK(walk(R"({"fill":"$shade","down_to":20})") == "down 100,200,300,400 200..220 2\n");
  CHECK(walk(R"({"fill":"$shade","radius":3})") == "rounddown 100,200,300,400 3 200..600 2\n");
  CHECK(walk(R"({"fill":"$face","radius":3})") == "round 100,200,300,400 3 1/0/0/1\n");
  CHECK(walk(R"({"fill":"99"})").empty());  // a number that is no entry draws nothing
  const auto pack = load(R"({"fill":"$face"})");
  CHECK(pack->palette_of("one") == 0);
  CHECK(pack->palette_of("two") == 1);
  CHECK(!pack->palette_of("three").has_value());
}

void texts_come_from_three_places() {
  const std::string_view ok = locale::tr().ok;
  CHECK(walk(R"({"text":"#ok","style":"body","color":"$face","h":16})") == "text " + std::string(ok) + " 100,200,300,16 12/16/400 1/0/0/1\n");
  CHECK(walk(R"({"text":"@nickname","style":"big","color":"$face"})") == "text Me 100,200,300,400 20/16/700 1/0/0/1\n");
  CHECK(walk(R"({"text":"plain words","style":"body","color":"$face","h":2})").starts_with("text plain words"));
  CHECK(!load(R"({"text":"#no_such_key","style":"body","color":"$face"})"));
  CHECK(!load(R"({"text":"@nobody","style":"body","color":"$face"})"));
}

void a_repeat_counts_with_i() {
  const std::string_view rows = R"({"repeat":"@seats","draw":[{"text":"@seat","style":"body","color":"$face","y":"i*20","h":20,"hit":"seat"}]})";
  CHECK(walk(rows).empty());
  CHECK(walk(rows, 0, 2) ==
        "text A 100,200,300,20 12/20/400 1/0/0/1\nhit 100,200,300,20 100\n"
        "text B 100,220,300,20 12/20/400 1/0/0/1\nhit 100,220,300,20 101\n");
}

void a_hit_lights_what_is_inside_it() {
  const std::string_view row = R"({"repeat":2,"draw":[{"group":[{"fill":"hot ? $ink : $face"}],"hit":"seat"}]})";
  const std::string lit = walk(row, 1, 0, 101);
  CHECK(lit == "fill 100,200,300,400 0/1/0/1\nhit 100,200,300,400 100\nfill 100,200,300,400 0/0/1/1\nhit 100,200,300,400 101\n");
}

void a_part_gets_its_box_label_and_arguments() {
  const std::string plain = walk(R"({"use":"button","text":"#ok","hit":"join","x":0.25,"y":0.25,"w":50,"h":20})");
  const std::string_view ok = locale::tr().ok;
  // It snaps its box, and the hit keeps the box it was asked for.
  CHECK(plain == "round 100,200,50,20 3 1/0/0/1\ntext " + std::string(ok) + " 103,200,50,20 12/20/400 1/0/0/1\nfill 100,200,30,20 1/0/0/1\nhit 100.25,200.25,50,20 51\n");
  const std::string hot = walk(R"({"use":"button","text":"x","hit":"join","args":{"off":1}})", 0, 0, 51, 51);
  CHECK(hot == "round 100,200,300,400 3 0/0/0/0.5\ntext x 102,200,300,400 12/400/400 0/0/0/0.5\nfill 100,200,20,400 1/0/0/1\nhit 100,200,300,400 51\n");
}

void a_part_is_inside_its_own_names() {
  // Its let does not reach the screen, and a screen's does not reach it.
  CHECK(!load(R"({"use":"button"},{"fill":"$face","x":"gap"})"));
  CHECK(!load(R"({"use":"button","args":{"nothing":1}})"));
  CHECK(!load(R"({"use":"nothing"})"));
  // Two uses keep to their own places.
  CHECK(walk(R"({"let":"keep","value":7},{"use":"inner","args":{"amount":"keep"}},{"use":"inner"},{"fill":"$face","x":"keep"})") ==
        "fill 100,200,7,400 1/0/0/1\nfill 100,200,1,400 1/0/0/1\nfill 107,200,300,400 1/0/0/1\n");
}

void picture_and_field_go_to_the_screen() {
  CHECK(walk(R"({"picture":"me","x":1,"y":2,"w":3,"h":4})") == "picture 31 101,202,3,4 1 0\n");
  CHECK(walk(R"({"picture":"me","opacity":0.5,"gray":1,"w":3,"h":4})") == "picture 31 100,200,3,4 0.5 1\n");
  CHECK(walk(R"({"field":"code","style":"body","color":"$face","placeholder":"type","placeholder_color":"$ink","caret":"$ink","selection":"$ink"})") ==
        "field 41 100,200,300,400 type 1/0/0/1 0/0/0/0.5\n");
  CHECK(!load(R"({"picture":"nobody"})"));
  CHECK(!load(R"({"field":"nobody","style":"body","color":"$face","placeholder":"t","placeholder_color":"$ink","caret":"$ink","selection":"$ink"})"));
}

void a_column_stacks_what_it_draws() {
  CHECK(walk(R"({"column":[{"fill":"$face","h":10},{"if":0,"fill":"$face","h":99},{"fill":"$face","h":5,"after":3},{"fill":"$face","h":1}],"id":"c","x":1,"y":2,"w":50},{"fill":"$face","y":"c.bottom","w":1,"h":1})") ==
        "fill 101,202,50,10 1/0/0/1\nfill 101,212,50,5 1/0/0/1\nfill 101,220,50,1 1/0/0/1\nfill 100,221,1,1 1/0/0/1\n");
  // A node can step back up by saying so, and a group inside starts again from the top.
  CHECK(walk(R"({"column":[{"fill":"$face","h":10},{"group":[{"fill":"$ink","h":2}],"y":"flow-1","h":5}],"w":5})") ==
        "fill 100,200,5,10 1/0/0/1\nfill 100,209,5,2 0/0/0/0.5\n");
}

void a_part_can_hold_a_list_and_say_how_tall_it_came_out() {
  const std::string out = walk(R"({"column":[{"use":"box","draw":[{"use":"row","text":"ab"},{"if":"@seats","use":"row","text":"cd"}],"after":14},{"fill":"$face","h":1}],"w":100})", 0, 1);
  CHECK(out == "text ab 110,214,20,20 12/20/400 1/0/0/1\ntext cd 110,234,20,20 12/20/400 1/0/0/1\nframe 100,200,100,60 0/0/0/0.5\nfill 100,274,100,1 1/0/0/1\n");
  // Without the second row it is that much shorter.
  const std::string less = walk(R"({"column":[{"use":"box","draw":[{"use":"row","text":"ab"},{"if":"@seats","use":"row","text":"cd"}],"after":14},{"fill":"$face","h":1}],"w":100})");
  CHECK(less == "text ab 110,214,20,20 12/20/400 1/0/0/1\nframe 100,200,100,40 0/0/0/0.5\nfill 100,254,100,1 1/0/0/1\n");
}

void a_part_sizes_itself_where_none_is_given() {
  CHECK(walk(R"({"use":"row","text":"abc"})") == "text abc 100,200,30,20 12/20/400 1/0/0/1\n");
  CHECK(walk(R"({"use":"row","text":"abc","w":7})") == "text abc 100,200,7,20 12/20/400 1/0/0/1\n");
  CHECK(walk(R"({"use":"row","text":"abc","right":100})") == "text abc 170,200,30,20 12/20/400 1/0/0/1\n");
}

void right_and_bottom_measure_from_the_far_edge() {
  CHECK(walk(R"({"fill":"$face","right":"W-1","bottom":"H-2","w":10,"h":20})") == "fill 389,578,10,20 1/0/0/1\n");
  CHECK(!load(R"({"fill":"$face","right":1,"x":1})"));
  CHECK(!load(R"({"fill":"$face","bottom":1,"y":1})"));
}

void hits_can_be_gated_and_counted_otherwise() {
  CHECK(walk(R"({"group":[{"fill":"hot ? $ink : $face"}],"hit":"join","w":1,"h":1,"live":0})", 0, 0, 51).starts_with("fill 100,200,1,1 1/0/0/1\nhit"));
  CHECK(walk(R"({"group":[{"fill":"$face"}],"hit":"join","w":1,"h":1,"hit_if":0})") == "fill 100,200,1,1 1/0/0/1\n");
  CHECK(walk(R"({"group":[{"fill":"$face"}],"hit":"seat","hit_index":7,"w":1,"h":1})").ends_with("hit 100,200,1,1 107\n"));
}

void numbers_are_read_for_the_round_of_a_repeat() {
  CHECK(walk(R"({"repeat":2,"draw":[{"fill":"$face","x":"@twice","w":1,"h":1}]})") == "fill 100,200,1,1 1/0/0/1\nfill 102,200,1,1 1/0/0/1\n");
}

void lines_paths_and_measures() {
  CHECK(walk(R"({"line":"$face","x":1,"y":2,"x2":3,"y2":4,"width":2})") == "line 101,202 103,204 1/0/0/1 2\n");
  CHECK(walk(R"({"path":"M0 0H1","color":"$face","x":1,"y":2})") == "path M0 0H1 101,202 x1 1/0/0/1 0 round\n");
  CHECK(walk(R"({"path":"M0 0H1","color":"$face","scale":0.5,"width":2,"caps":"flat"})") == "path M0 0H1 100,200 x0.5 1/0/0/1 2 flat\n");
  CHECK(!load(R"({"path":"M0","color":"$face","caps":"square"})"));
  // 40 wide at 10 a letter in 15 makes three lines of 16.
  CHECK(walk(R"J({"fill":"$face","w":1,"h":"texth('big', 'abcd', 15)"})J") == "fill 100,200,1,48 1/0/0/1\n");
  CHECK(walk(R"J({"fill":"$face","x":"snap(1.4)","w":1,"h":1})J") == "fill 101,200,1,1 1/0/0/1\n");
}

void slots_reports_and_notes_go_to_the_screen() {
  CHECK(walk(R"({"slot":"list","numbers":[3],"styles":["body"],"colors":["$face"]})") == "slot 61 100,200,300,400 1/1/1 3\n");
  CHECK(walk(R"({"report":"height","value":"W+1"})") == "report 71 301\n");
  CHECK(walk(R"({"fill":"$face","note":"where","w":1,"h":1})") == "note 81/0 100,200,1,1\nfill 100,200,1,1 1/0/0/1\n");
  CHECK(!load(R"({"slot":"nothing"})"));
  CHECK(!load(R"({"report":"nothing","value":1})"));
  CHECK(!load(R"({"fill":"$face","note":"nothing"})"));
}

void a_part_can_be_drawn_alone() {
  const auto pack = load(R"({"fill":"$face"})");
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
  CHECK(walk(R"({"group":[{"fill":"$face"}],"clip":true,"w":5,"h":6})") == "clip 100,200,5,6\nfill 100,200,5,6 1/0/0/1\nunclip\n");
}

void a_bad_node_is_refused() {
  CHECK(!load(R"(1)"));
  CHECK(!load(R"({})"));
  CHECK(!load(R"({"fill":"$face","frame":"$ink"})"));
  CHECK(!load(R"({"fill":"$face","colour":"$ink"})"));
  CHECK(!load(R"({"frame":"$face","radius":1})"));
  CHECK(!load(R"({"fill":"$nobody"})"));
  CHECK(!load(R"({"fill":"$face","hit":"nobody"})"));
  CHECK(!load(R"({"stroke":"$face"})"));
  CHECK(!load(R"({"text":"x","style":"nobody","color":"$face"})"));
  CHECK(!load(R"({"repeat":2})"));
  CHECK(!load(R"({"fill":"$face","x":[1]})"));
  CHECK(walk(R"({"fill":"$face","x":true,"alpha":0.5})") == "fill 101,200,300,400 1/0/0/0.5\n");
  CHECK(!load(R"({"fill":"$face","//":"a comment is a comment now"})"));
}

std::expected<Pack, std::string> load_with(std::string_view from, std::string_view to) {
  std::string pack = test_pack;
  const std::size_t at = pack.find(from);
  CHECK(at != std::string::npos);
  pack.replace(at, from.size(), to);
  return load_pack(files(pack, fill_list(R"({"fill":"$face"})")), std::span(&test_schema, 1));
}

void a_bad_pack_is_refused() {
  CHECK(load_with("\"format\": 1", "\"format\": 1").has_value());
  CHECK(!load_with("\"format\": 1", "\"format\": 2"));
  CHECK(!load_with("\"format\": 1,", ""));
  CHECK(!load_with("\"id\": \"test\",", ""));
  CHECK(!load_with("#ff0000", "red"));
  CHECK(!load_with("#ff0000", "#ff00"));
  CHECK(!load_with("\"face\": \"#00ff00\"", "\"other\": \"#00ff00\""));
  CHECK(load_with("\"ink\": \"#00000080\"", "\"ink\": {\"mix\": [\"face\", \"face\", 50]}").has_value());
  CHECK(load_with("\"ink\": \"#00000080\"", "\"ink\": {\"alpha\": [\"face\", 0.5]}").has_value());
  CHECK(!load_with("\"ink\": \"#00000080\"", "\"ink\": {\"mix\": [\"nobody\", \"face\", 50]}"));
  CHECK(!load_with("\"ink\": \"#00000080\"", "\"ink\": {\"alpha\": [\"shade\", 0.5]}"));
  CHECK(!load_with("\"face\": \"#00ff00\"", "\"face\": [[0, \"#000000\"], [1, \"#000000\"]]"));  // a colour in the first
  CHECK(!load_with("\"size\": 20,", ""));
  CHECK(!load_with("\"align\": \"center\"", "\"align\": \"middle\""));
  CHECK(!load_pack([](std::string_view) { return std::optional<std::string>{}; }, std::span(&test_schema, 1)));
  CHECK(!load_pack(files("not json", ""), std::span(&test_schema, 1)));
  CHECK(!load_pack(files(test_pack, "[]"), std::span(&test_schema, 1)));
  CHECK(!load_pack(files(test_pack, "{}"), std::span(&test_schema, 1)));
}

void an_error_says_where() {
  const auto pack = load(R"({"fill":"$nobody"})");
  CHECK(!pack && pack.error().find("screens/home.jsonc") != std::string::npos && pack.error().find("nobody") != std::string::npos);
  const auto bad = load_with("#ff0000", "red");
  CHECK(!bad && bad.error().find("pack.jsonc") != std::string::npos && bad.error().find("face") != std::string::npos);
}

void a_file_is_jsonc_and_names_its_schema() {
  const auto pack = load_pack(files(test_pack, R"J({
    "$schema": "../schema/screen.home.schema.json",
    // A comment, and a comma after the last item.
    "draw": [{"fill": "$face",},],
  })J"), std::span(&test_schema, 1));
  CHECK(pack.has_value());
  const auto broken = load_pack(files(test_pack, "{\n  \"draw\": [\n    {\"fill\" \"$face\"}\n  ]\n}"), std::span(&test_schema, 1));
  CHECK(!broken && broken.error().find("screens/home.jsonc:3:13") != std::string::npos);
}

void a_screen_without_a_file_is_left_out() {
  const auto pack = load_pack(files(test_pack, ""), std::span(&test_schema, 1));
  CHECK(pack.has_value());
  CHECK(pack->screen("home") == nullptr);
}

void sizes_are_limited() {
  // A file too large.
  CHECK(!load_pack(files(std::string(max_file_bytes + 1, ' ') + "{}", ""), std::span(&test_schema, 1)));
  // Nodes too deep.
  std::string deep = R"({"fill":"$face"})";
  for (int i = 0; i < max_node_depth + 1; ++i) deep = "{\"group\":[" + deep + "]}";
  CHECK(!load(deep));
  // Too many names.
  std::string names;
  for (int i = 0; i <= max_frame_slots; ++i) names += (i ? "," : "") + ("{\"let\":\"n" + std::to_string(i) + "\",\"value\":1}");
  CHECK(!load(names));
}

void a_walk_ends_however_parts_multiply() {
  // Each part uses the one before it twice, which makes 2^40 nodes.
  std::string pack = R"({"format":1,"id":"t","palettes":{"one":{"face":"#000000"}},"styles":{},"parts":{"p0":{"draw":[{"fill":"$face"}]})";
  for (int i = 1; i <= 40; ++i) {
    const std::string before = "p" + std::to_string(i - 1);
    pack += ",\"p" + std::to_string(i) + "\":{\"draw\":[{\"use\":\"" + before + "\"},{\"use\":\"" + before + "\"}]}";
  }
  pack += "}}";
  const auto loaded = load_pack(files(pack, fill_list(R"({"use":"p40"})")), std::span(&test_schema, 1));
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

void test_cursors_are_all_there_or_refused() {
  const auto with = [](std::string cursors) {
    const std::map<std::string, std::string> files = {{"pack.jsonc", R"J({"format":1,"id":"t","palettes":{"a":{"ink":"#000000"}},"styles":{},"parts":{"p":{"draw":[{"fill":"$ink"}]}},"cursors":)J" + std::move(cursors) + "}"}};
    return load_pack([files](std::string_view path) -> std::optional<std::string> {
      const auto found = files.find(std::string(path));
      if (found == files.end()) return std::nullopt;
      return found->second;
    }, {});
  };
  std::string all = "{";
  for (const std::string_view name : cursor_names) all += '"' + std::string(name) + R"(":{"part":"p","hot":[2,3]},)";
  all.back() = '}';
  const auto good = with(all);
  CHECK(good.has_value());
  if (good) {
    CHECK(good->cursors.has_value());
    CHECK((*good->cursors)[static_cast<std::size_t>(CursorKind::IBeam)].hot_y == 3);
    CHECK(!good->cursors_hard);
  }
  CHECK(!with(R"({"arrow":{"part":"p","hot":[2,3]}})").has_value());
  std::string hard = all;
  hard.insert(1, R"("hard":true,)");
  const auto with_hard_edges = with(hard);
  CHECK(with_hard_edges.has_value() && with_hard_edges->cursors_hard);
  hard.replace(hard.find("true"), 4, "1");
  CHECK(!with(hard).has_value());
  std::string no_part = all;
  no_part.replace(no_part.find("\"p\""), 3, "\"nobody\"");
  CHECK(!with(no_part).has_value());
  std::string outside = all;
  outside.replace(outside.find("[2,3]"), 5, "[2,33]");
  CHECK(!with(outside).has_value());
}

int main() {
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
  a_file_is_jsonc_and_names_its_schema();
  a_screen_without_a_file_is_left_out();
  sizes_are_limited();
  a_walk_ends_however_parts_multiply();
  test_cursors_are_all_there_or_refused();
  return crpp_test_result();
}
