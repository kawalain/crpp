#include "crpp/skin/cskin.hpp"

#include <cstdio>
#include <string>

#include "check.hpp"

using namespace crpp::skin;
using crpp::store::Json;

namespace {

Json json(std::string_view text) {
  auto parsed = Json::parse(text);
  CHECK(parsed.has_value());
  return parsed ? std::move(*parsed) : Json();
}

// The screen's tree as the loader would get it from JSON.
bool screen_is(std::string_view source, std::string_view draw) {
  const auto read = read_cskin(source, SourceKind::Screen);
  if (!read) {
    std::fprintf(stderr, "%zu:%zu: %s\n", read.error().line, read.error().column, read.error().what.c_str());
    return false;
  }
  const Json want = json(std::string(R"({"draw":)") + std::string(draw) + "}");
  if (*read == want) return true;
  std::fprintf(stderr, "got  %s\nwant %s\n", read->dump().c_str(), want.dump().c_str());
  return false;
}

bool pack_is(std::string_view source, std::string_view want_text) {
  const auto read = read_cskin(source, SourceKind::Pack);
  if (!read) {
    std::fprintf(stderr, "%zu:%zu: %s\n", read.error().line, read.error().column, read.error().what.c_str());
    return false;
  }
  const Json want = json(want_text);
  if (*read == want) return true;
  std::fprintf(stderr, "got  %s\nwant %s\n", read->dump().c_str(), want.dump().c_str());
  return false;
}

// Where it stops, and that it says something.
bool refused_at(std::string_view source, std::size_t line, std::size_t column, SourceKind kind = SourceKind::Screen) {
  const auto read = read_cskin(source, kind);
  if (read) return false;
  if (read.error().line == line && read.error().column == column && !read.error().what.empty()) return true;
  std::fprintf(stderr, "refused at %zu:%zu: %s\n", read.error().line, read.error().column, read.error().what.c_str());
  return false;
}

void a_line_is_a_node() {
  CHECK(screen_is("fill ink radius 4", R"J([{"fill":"ink","radius":4}])J"));
  CHECK(screen_is("fill ink (1, 2, 3, 4)\nstroke ink (5, 6) width 2 ellipse", R"J([{"fill":"ink","x":1,"y":2,"w":3,"h":4},{"stroke":"ink","x":5,"y":6,"width":2,"ellipse":true}])J"));
  CHECK(screen_is("", "[]"));
  CHECK(screen_is("// only a comment\n\n", "[]"));
  CHECK(screen_is("fill ink clip false", R"J([{"fill":"ink","clip":false}])J"));
}

void values_keep_their_expressions() {
  CHECK(screen_is("fill @dark ? ink : face radius 2*u", R"J([{"fill":"@dark ? ink : face","radius":"2*u"}])J"));
  CHECK(screen_is("fill mix(ink, face, 40%) (0, 0, @W - 2, @H/2)", R"J([{"fill":"mix(ink, face, 40%)","x":0,"y":0,"w":"@W - 2","h":"@H/2"}])J"));
  CHECK(screen_is("fill ink dy -4 radius 150ms", R"J([{"fill":"ink","dy":-4,"radius":150}])J"));
  CHECK(screen_is("fill ink alpha 40%", R"J([{"fill":"ink","alpha":"40%"}])J"));
  CHECK(screen_is("fill ink w textw(tip, @tr.find, 600)+30", R"J([{"fill":"ink","w":"textw(tip, @tr.find, 600)+30"}])J"));
  CHECK(screen_is("fill ink w box.right", R"J([{"fill":"ink","w":"box.right"}])J"));
}

void texts_and_names() {
  CHECK(screen_is("text @tr.home_create style center color ink", R"J([{"text":"#home_create","style":"center","color":"ink"}])J"));
  CHECK(screen_is("text @greeting style center color ink", R"J([{"text":"@greeting","style":"center","color":"ink"}])J"));
  const char* const quoted = "text \"a \\\"b\\\"\" style plain color ink";  // text "a \"b\"" ...
  CHECK(screen_is(quoted, "[{\"text\":\"a \\\"b\\\"\",\"style\":\"plain\",\"color\":\"ink\"}]"));
  CHECK(screen_is(R"J(path "M10 22h23" color ink caps round)J", R"J([{"path":"M10 22h23","color":"ink","caps":"round"}])J"));
  CHECK(screen_is("slot volume numbers (1, @W) styles (body, tip)", R"J([{"slot":"volume","numbers":[1,"@W"],"styles":["body","tip"]}])J"));
}

void children_go_in_braces() {
  CHECK(screen_is("hit join (1, 2, 3, 4) {\n  fill ink\n}", R"J([{"hit":"join","x":1,"y":2,"w":3,"h":4,"group":[{"fill":"ink"}]}])J"));
  CHECK(screen_is("hit code", R"J([{"hit":"code","group":[]}])J"));
  CHECK(screen_is("group", R"J([{"group":[]}])J"));
  CHECK(screen_is("hit a {}", R"J([{"hit":"a","group":[]}])J"));
  CHECK(screen_is("column clip {\n  fill ink\n}", R"J([{"clip":true,"column":[{"fill":"ink"}]}])J"));
  CHECK(screen_is("repeat 3 {\n  fill ink\n}", R"J([{"repeat":3,"draw":[{"fill":"ink"}]}])J"));
  CHECK(screen_is("if x > 0 {\n  fill ink\n  group {\n    content\n  }\n}", R"J([{"if":"x > 0","group":[{"fill":"ink"},{"group":[{"content":true}]}]}])J"));
}

void a_deeper_line_goes_on() {
  CHECK(screen_is("fill ink\n    radius 4\nfill face", R"J([{"fill":"ink","radius":4},{"fill":"face"}])J"));
  CHECK(screen_is("hit a {\n  fill ink\n      radius 4\n  fill face\n}", R"J([{"hit":"a","group":[{"fill":"ink","radius":4},{"fill":"face"}]}])J"));
  CHECK(screen_is("fill mix(ink,\n  face, 40%)", R"J([{"fill":"mix(ink, face, 40%)"}])J"));
}

void lets_parts_and_pairs() {
  CHECK(screen_is("let over = ease(@hot, 150ms)", R"J([{"let":"over","to":"@hot","ms":150}])J"));
  CHECK(screen_is("let over = ease(@hot, 150ms) ms_off 90ms curve linear first jump", R"J([{"let":"over","to":"@hot","ms":150,"ms_off":90,"curve":"linear","first":"jump"}])J"));
  CHECK(screen_is("let t = ease(x)", R"J([{"let":"t","value":"ease(x)"}])J"));
  CHECK(screen_is("let t = ease(x, 1) + 1", R"J([{"let":"t","value":"ease(x, 1) + 1"}])J"));
  CHECK(screen_is("set widest = max(widest, @w)\nreport size = 4", R"J([{"set":"widest","value":"max(widest, @w)"},{"report":"size","value":4}])J"));
  CHECK(screen_is("use tip(shown = shown, at = 1) (0, 0, 40, 32) text @tr.find", R"J([{"use":"tip","args":{"shown":"shown","at":1},"x":0,"y":0,"w":40,"h":32,"text":"#find"}])J"));
  CHECK(screen_is("text @tr.find use tip", R"J([{"text":"#find","use":"tip"}])J"));
  CHECK(screen_is("use list {\n  fill ink\n}", R"J([{"use":"list","draw":[{"fill":"ink"}]}])J"));
  CHECK(screen_is("line ink (1, 2) to (3, 4) width 2", R"J([{"line":"ink","x":1,"y":2,"x2":3,"y2":4,"width":2}])J"));
  CHECK(screen_is("group scale 1+g about (5, 6)", R"J([{"scale":"1+g","about_x":5,"about_y":6,"group":[]}])J"));
  CHECK(screen_is(R"J(hit a hit_path "M0 0h1" hit_path_at (-6, 1))J", R"J([{"hit":"a","hit_path":"M0 0h1","hit_path_x":-6,"hit_path_y":1,"group":[]}])J"));
}

void a_pack_has_its_statements() {
  CHECK(pack_is(R"J(pack retro format 1 extends base
names ko "레트로" en "Retro"
theme classic names ko "고전" en "Classic" dark classic_dark
theme luna names en "Luna"
    overlay luna palette blue
default_theme classic
constants {
  own_frame 1
  gap -2
}
fonts {
  korean "Gulim" "Malgun Gothic"
  other "Tahoma"
  caption "Tahoma"
  korean_size 12 other_size 11
}
style center size 44 line_height @font_size*2 weight 700 align center wrap
)J",
                R"J({"id":"retro","format":1,"extends":"base","names":{"ko":"레트로","en":"Retro"},
                    "themes":{"classic":{"names":{"ko":"고전","en":"Classic"},"dark":"classic_dark"},"luna":{"names":{"en":"Luna"},"overlay":"luna","palette":"blue"}},
                    "default_theme":"classic","constants":{"own_frame":1,"gap":-2},
                    "fonts":{"korean":["Gulim","Malgun Gothic"],"other":["Tahoma"],"caption":["Tahoma"],"korean_size":12,"other_size":11},
                    "styles":{"center":{"size":44,"line_height":"@font_size*2","weight":700,"align":"center","wrap":true}}})J"));
  CHECK(pack_is(R"J(part cloud(scale = 1, shadow = 0) w 10 snap {
  path "M0 0" color ink scale scale
}
cursors hard {
  arrow part cloud hot (0, 1)
}
palette day {
  ink #000000
  soft mix(ink, face, 70%)
  half alpha(ink, 0.5)
  sky stops(0: #ffffff, 1: #eff8ff)
}
palette night extends day {
  ink #ffffffcc
}
)J",
                R"J({"parts":{"cloud":{"args":{"scale":1,"shadow":0},"w":10,"snap":true,"draw":[{"path":"M0 0","color":"ink","scale":"scale"}]}},
                    "cursors":{"hard":true,"arrow":{"part":"cloud","hot":[0,1]}},
                    "palettes":{"day":{"ink":"#000000","soft":{"mix":["ink","face",70]},"half":{"alpha":["ink",0.5]},"sky":[[0,"#ffffff"],[1,"#eff8ff"]]},
                                "night":{"extends":"day","ink":"#ffffffcc"}}})J"));
}

void mistakes_say_where() {
  CHECK(refused_at("fill ink\nfil ink", 2, 1));
  CHECK(refused_at("fill ink radius", 1, 16));
  CHECK(refused_at("hit a {\n  fill ink\n", 3, 1));
  CHECK(refused_at("fill ink }", 1, 10));
  CHECK(refused_at("fill #12345", 1, 6));
  CHECK(refused_at("fill 12px", 1, 6));
  CHECK(refused_at("fill ink /* no */", 1, 10));
  CHECK(refused_at("fill ink (1, 2, 3)", 1, 10));
  CHECK(refused_at("fill ink radius 1 radius 2", 1, 19));
  CHECK(refused_at("fill ink stroke ink", 1, 10));
  CHECK(refused_at("fill ink {\n}", 1, 10));
  CHECK(refused_at("text \"open", 1, 6));
  CHECK(refused_at("text @tr.a.b", 1, 11));
  CHECK(refused_at("pack x format one", 1, 15, SourceKind::Pack));
  CHECK(refused_at("palette p {\n  a mix(b, c, 70)\n}", 2, 15, SourceKind::Pack));
  CHECK(refused_at("screen home", 1, 1, SourceKind::Pack));
}

void depth_is_limited() {
  std::string deep;
  for (int i = 0; i < 40; ++i) deep += "group {\n";
  for (int i = 0; i < 40; ++i) deep += "}\n";
  CHECK(!read_cskin(deep, SourceKind::Screen));
  std::string nested = "fill ";
  for (int i = 0; i < 40; ++i) nested += "(";
  nested += "1";
  for (int i = 0; i < 40; ++i) nested += ")";
  CHECK(!read_cskin(nested, SourceKind::Screen));
}

}  // namespace

int main() {
  a_line_is_a_node();
  values_keep_their_expressions();
  texts_and_names();
  children_go_in_braces();
  a_deeper_line_goes_on();
  lets_parts_and_pairs();
  a_pack_has_its_statements();
  mistakes_say_where();
  depth_is_limited();
  return crpp_test_result();
}
