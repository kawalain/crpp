#pragma once
// The pack language: a Skin Pack's .cskin files, read into the tree the loader takes.
//
// A line is a node or an item of the pack: a word, then `key value` pairs, and children in { }.
// A line indented deeper than the one it follows, with no { between, goes on with it. A box is
// (x, y, w, h) and a point (x, y); `about (x, y)`, `to (x, y)` and `hit_path_at (x, y)` set the
// pairs of those keys. Values are written as in an expression: names, numbers with 150ms or
// 40%, @ before every name the exe defines, @tr.key for wording, "words" and #rrggbb.
//
//   let over = ease(@hot, 150ms)      a transition: `to` and `ms`; any other value is `value`
//   use tip(shown = shown) (0, 0, 40, 32) text @tr.home_find_public
//   hit join (221, 10, 52, 60) { … }  no kind and children: a group
//
// pack.cskin holds `pack <id> format 1 [extends <id>]`, `names ko "…" en "…"`, `theme <id> names …
// [palette p] [dark d] [overlay o]`, `default_theme <id>`, `constants { name 1 … }`, `fonts { korean
// "…" other "…" caption "…" korean_size 13 other_size 12 }`, `style <name> size … line_height …`,
// `part <name>(arg = 1, …) [w …] [h …] [snap] { … }`, `cursors [hard] { arrow part p hot (1, 1) … }`
// and `palette <name> [extends <base>] { white #ffffff  soft mix(a, b, 70%)  half alpha(a, 0.5)
// sky stops(0: #ffffff, 1: #eff8ff) }`. A screen's file is its nodes.
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "crpp/store/json.hpp"

namespace crpp::skin {

// Both counted from 1; a column counts bytes.
struct SourceError {
  std::size_t line = 0, column = 0;
  std::string what;
};

enum class SourceKind : std::uint8_t { Pack, Screen };

// Expressions stay text, in the pack form of expr.hpp, for the loader to compile.
[[nodiscard]] std::expected<store::Json, SourceError> read_cskin(std::string_view text, SourceKind kind);

}  // namespace crpp::skin
