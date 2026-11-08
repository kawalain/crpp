#pragma once
// Words and paragraphs in text, for moving by word and for a double or triple click, in a
// field and in text that is only read. Pure.
#include <cstddef>
#include <string_view>
#include <utility>

namespace crpp::ui {

// Where Ctrl+Right goes: past the word at pos, then past the spaces after it. A line break
// stops every run.
[[nodiscard]] std::size_t next_word(std::wstring_view s, std::size_t pos);
// Where Ctrl+Left goes: back past spaces, then to the start of the word before them.
[[nodiscard]] std::size_t previous_word(std::wstring_view s, std::size_t pos);
// What a press takes around pos, as [start, end): the place itself for one click, the word
// (or the run of spaces) for two, the paragraph between line breaks for three.
[[nodiscard]] std::pair<std::size_t, std::size_t> unit_at(std::wstring_view s, std::size_t pos, int clicks);

}  // namespace crpp::ui
