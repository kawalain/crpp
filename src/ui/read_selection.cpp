#include "crpp/ui/read_selection.hpp"

#include <algorithm>

#include "crpp/ui/text_units.hpp"

namespace crpp::ui {

std::pair<TextPoint, TextPoint> ReadSelection::unit(TextPoint at, const TextOf& text_of) const {
  const auto [lo, hi] = unit_at(text_of(at.block), at.offset, clicks_);
  return {{at.block, lo}, {at.block, hi}};
}

void ReadSelection::press(TextPoint at, int clicks, bool extend, const TextOf& text_of) {
  clicks_ = std::clamp(clicks, 1, 3);
  const auto [lo, hi] = unit(at, text_of);
  if (extend && !empty()) {
    // Shift goes on from where the selection began.
    origin_lo_ = origin_hi_ = anchor_;
    caret_ = at < anchor_ ? lo : hi;
    return;
  }
  origin_lo_ = anchor_ = lo;
  origin_hi_ = caret_ = hi;
}

void ReadSelection::drag_to(TextPoint at, const TextOf& text_of) {
  const auto [lo, hi] = unit(at, text_of);
  if (at < origin_lo_) {
    anchor_ = origin_hi_;
    caret_ = lo;
  } else {
    anchor_ = origin_lo_;
    caret_ = std::max(hi, origin_hi_);
  }
}

void ReadSelection::select(TextPoint from, TextPoint to) {
  clicks_ = 1;
  origin_lo_ = origin_hi_ = anchor_ = from;
  caret_ = to;
}

std::optional<std::pair<std::size_t, std::size_t>> ReadSelection::in_block(std::size_t block, std::size_t length) const {
  if (empty()) return std::nullopt;
  const TextPoint lo = start(), hi = end();
  if (block < lo.block || block > hi.block) return std::nullopt;
  const std::size_t from = block == lo.block ? std::min(lo.offset, length) : 0;
  const std::size_t to = block == hi.block ? std::min(hi.offset, length) : length;
  if (from >= to) return std::nullopt;
  return std::pair{from, to};
}

std::wstring ReadSelection::text(const TextOf& text_of) const {
  std::wstring out;
  if (empty()) return out;
  const TextPoint lo = start(), hi = end();
  bool first = true;
  for (std::size_t block = lo.block; block <= hi.block; ++block) {
    const std::wstring_view whole = text_of(block);
    if (whole.empty()) continue;  // gone: no line for it
    const std::size_t from = block == lo.block ? std::min(lo.offset, whole.size()) : 0;
    const std::size_t to = block == hi.block ? std::min(hi.offset, whole.size()) : whole.size();
    if (!first) out += L'\n';
    first = false;
    if (from < to) out += whole.substr(from, to - from);
  }
  return out;
}

}  // namespace crpp::ui
