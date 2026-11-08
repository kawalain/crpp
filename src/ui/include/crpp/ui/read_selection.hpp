#pragma once
// A selection in text that is only read, such as chat messages or the lines of the changelog.
// The text is in blocks numbered in order; the owner says what each holds, so the selection
// keeps to its blocks while others come and go. Pure.
#include <compare>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace crpp::ui {

struct TextPoint {
  std::size_t block = 0;
  std::size_t offset = 0;  // UTF-16 units into the block's text
  auto operator<=>(const TextPoint&) const = default;
};

class ReadSelection {
 public:
  // A block's text; empty for one that is gone.
  using TextOf = std::function<std::wstring_view(std::size_t block)>;

  // A press at a place: one click starts there, two take the word and three the paragraph;
  // Shift extends what is selected. A drag that follows goes by the same unit.
  void press(TextPoint at, int clicks, bool extend, const TextOf& text_of);
  void drag_to(TextPoint at, const TextOf& text_of);
  void select(TextPoint from, TextPoint to);
  void clear() { anchor_ = caret_ = {}; }

  [[nodiscard]] bool empty() const { return anchor_ == caret_; }
  [[nodiscard]] TextPoint start() const { return std::min(anchor_, caret_); }
  [[nodiscard]] TextPoint end() const { return std::max(anchor_, caret_); }
  // What is selected of a block of that length, as [start, end).
  [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> in_block(std::size_t block, std::size_t length) const;
  // What a copy takes: each block's part, one per line.
  [[nodiscard]] std::wstring text(const TextOf& text_of) const;

 private:
  [[nodiscard]] std::pair<TextPoint, TextPoint> unit(TextPoint at, const TextOf& text_of) const;

  TextPoint anchor_, caret_;
  int clicks_ = 1;
  TextPoint origin_lo_, origin_hi_;
};

}  // namespace crpp::ui
