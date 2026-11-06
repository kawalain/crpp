#pragma once
// The Markdown that GitHub release notes use, read into blocks to draw. Anything else
// stays as the text it is; nothing in the notes can make the app do anything, and
// links are shown by their text only.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace crpp::update {

struct NoteRun {
  std::string text;
  bool bold = false;
};

enum class NoteKind : std::uint8_t { Heading, Bullet, Paragraph };

struct NoteBlock {
  NoteKind kind = NoteKind::Paragraph;
  int indent = 0;      // nesting of a bullet, from 0
  std::string marker;  // "3." for a numbered item; empty draws a bullet
  std::vector<NoteRun> runs;
};

[[nodiscard]] std::vector<NoteBlock> parse_notes(std::string_view markdown);

}  // namespace crpp::update
