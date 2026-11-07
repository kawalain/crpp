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
constexpr Name test_numbers[] = {{"seats", 21}};
constexpr Name test_pictures[] = {{"me", 31}};
constexpr Name test_fields[] = {{"code", 41}};
constexpr Name test_hits[] = {{"join", 51}, {"seat", 100}};
constexpr ScreenSchema test_schema{"home", test_texts, test_numbers, test_pictures, test_fields, test_hits};

const char* const test_pack = R"({
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
    "inner": {
      "args": {"amount": 1},
      "draw": [{"fill": "$face", "w": "amount"}]
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
})";

std::string fill_list(std::string_view nodes) { return "{\"draw\":[" + std::string(nodes) + "]}"; }

ReadFile files(std::string pack, std::string home) {
  return [pack = std::move(pack), home = std::move(home)](std::string_view path) -> std::optional<std::string> {
    if (path == "pack.json") return pack;
    if (path == "screens/home.json" && !home.empty()) return home;
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
  float line() override { return 2; }
  float font_size() override { return 12; }
  float text_width(std::string_view text, const TextLook&) override { return static_cast<float>(text.size()) * 10; }
  void fill(Box b, Color c) override { add("fill " + str(b) + " " + str(c)); }
  void fill_down(Box b, std::span<const GradientStop> s) override { add("down " + str(b) + " " + std::to_string(s.size())); }
  void fill_round(Box b, float r, Color c) override { add("round " + str(b) + " " + str(r) + " " + str(c)); }
  void fill_round_down(Box b, float r, std::span<const GradientStop> s) override { add("rounddown " + str(b) + " " + str(r) + " " + std::to_string(s.size())); }
  void frame(Box b, Color c) override { add("frame " + str(b) + " " + str(c)); }
  void stroke_round(Box b, float r, Color c, float w) override { add("stroke " + str(b) + " " + str(r) + " " + str(c) + " " + str(w)); }
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
  float number(int id) override { return id == 21 ? static_cast<float>(seats) : 0; }
  void picture(int id, Box b) override { log->add("picture " + std::to_string(id) + " " + Log::str(b)); }
  void field(int id, Box b, const FieldLook& l) override {
    log->add("field " + std::to_string(id) + " " + Log::str(b) + " " + std::string(l.placeholder) + " " + Log::str(l.color) + " " + Log::str(l.selection));
  }
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

void conditions_skip_what_they_guard() {
  CHECK(walk(R"({"if":"@seats>0","fill":"$face"},{"if":"@seats==0","fill":"$ink"})").starts_with("fill 100,200,300,400 0/0/0/0.5"));
  CHECK(walk(R"({"if":"@seats>0","fill":"$face"},{"if":"@seats==0","fill":"$ink"})", 0, 1).starts_with("fill 100,200,300,400 1/0/0/1"));
  // One that was skipped reads as zeros.
  CHECK(walk(R"({"if":0,"fill":"$face","id":"a","x":5},{"fill":"$face","x":"a.x+1"})") == "fill 101,200,300,400 1/0/0/1\n");
}

void palettes_choose_colours_and_gradients() {
  CHECK(walk(R"({"fill":"$face"})", 1) == "fill 100,200,300,400 0/1/0/1\n");
  CHECK(walk(R"({"fill":"$ink"})", 1) == "fill 100,200,300,400 0/0/1/1\n");
  CHECK(walk(R"({"fill":"$shade"})") == "down 100,200,300,400 2\n");
  CHECK(walk(R"({"fill":"$shade","radius":3})") == "rounddown 100,200,300,400 3 2\n");
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
  CHECK(walk(R"({"picture":"me","x":1,"y":2,"w":3,"h":4})") == "picture 31 101,202,3,4\n");
  CHECK(walk(R"({"field":"code","style":"body","color":"$face","placeholder":"type","placeholder_color":"$ink","caret":"$ink","selection":"$ink"})") ==
        "field 41 100,200,300,400 type 1/0/0/1 0/0/0/0.5\n");
  CHECK(!load(R"({"picture":"nobody"})"));
  CHECK(!load(R"({"field":"nobody","style":"body","color":"$face","placeholder":"t","placeholder_color":"$ink","caret":"$ink","selection":"$ink"})"));
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
  CHECK(!load(R"({"fill":"$face","x":true})"));
  CHECK(load(R"({"fill":"$face","//":"a comment"})").has_value());
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
  CHECK(!pack && pack.error().find("screens/home.json") != std::string::npos && pack.error().find("nobody") != std::string::npos);
  const auto bad = load_with("#ff0000", "red");
  CHECK(!bad && bad.error().find("pack.json") != std::string::npos && bad.error().find("face") != std::string::npos);
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
    float text_width(std::string_view, const TextLook&) override { return 0; }
    void fill(Box, Color) override { ++fills; }
    void fill_down(Box, std::span<const GradientStop>) override {}
    void fill_round(Box, float, Color) override {}
    void fill_round_down(Box, float, std::span<const GradientStop>) override {}
    void frame(Box, Color) override {}
    void stroke_round(Box, float, Color, float) override {}
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

int main() {
  boxes_are_measured_from_the_box_around_them();
  siblings_read_each_others_boxes();
  a_let_is_read_further_on_and_inside();
  conditions_skip_what_they_guard();
  palettes_choose_colours_and_gradients();
  texts_come_from_three_places();
  a_repeat_counts_with_i();
  a_hit_lights_what_is_inside_it();
  a_part_gets_its_box_label_and_arguments();
  a_part_is_inside_its_own_names();
  picture_and_field_go_to_the_screen();
  a_clip_goes_around_a_group();
  a_bad_node_is_refused();
  a_bad_pack_is_refused();
  an_error_says_where();
  a_screen_without_a_file_is_left_out();
  sizes_are_limited();
  a_walk_ends_however_parts_multiply();
  return crpp_test_result();
}
