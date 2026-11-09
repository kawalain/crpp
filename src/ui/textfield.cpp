#include "crpp/ui/textfield.hpp"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <vector>

#include "crpp/ui/text_units.hpp"

namespace crpp::ui {

namespace {

bool is_control(wchar_t c) { return c < 0x20 || c == 0x7F; }

bool is_low_surrogate(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

std::size_t code_points(std::wstring_view s) {
  return static_cast<std::size_t>(std::ranges::count_if(s, [](wchar_t c) { return !is_low_surrogate(c); }));
}

// The first n code points of s, never half of a surrogate pair.
std::wstring_view first_code_points(std::wstring_view s, std::size_t n) {
  std::size_t end = 0;
  for (std::size_t taken = 0; end < s.size() && taken < n; ++taken) {
    ++end;
    if (end < s.size() && is_low_surrogate(s[end])) ++end;
  }
  return s.substr(0, end);
}

// Whole UTF-16 code points only: never leave a caret between surrogates.
std::size_t step(const std::wstring& s, std::size_t pos, int dir) {
  if (dir < 0) {
    if (pos == 0) return 0;
    --pos;
    if (pos > 0 && s[pos] >= 0xDC00 && s[pos] <= 0xDFFF && s[pos - 1] >= 0xD800 && s[pos - 1] <= 0xDBFF) --pos;
    return pos;
  }
  if (pos >= s.size()) return s.size();
  ++pos;
  if (pos < s.size() && s[pos] >= 0xDC00 && s[pos] <= 0xDFFF && s[pos - 1] >= 0xD800 && s[pos - 1] <= 0xDBFF) ++pos;
  return pos;
}

}  // namespace

std::size_t TextField::length() const { return code_points(text_); }

void TextField::set_text(std::wstring_view s) {
  text_.assign(s);
  composition_.clear();
  caret_ = anchor_ = text_.size();
}

void TextField::erase_selection() {
  const auto lo = std::min(caret_, anchor_), hi = std::max(caret_, anchor_);
  text_.erase(lo, hi - lo);
  caret_ = anchor_ = lo;
}

void TextField::insert(std::wstring_view s) {
  std::wstring clean;
  for (const wchar_t c : s) {
    if (is_control(c) && !(multiline && c == 0x0A)) continue;
    const wchar_t typed = uppercase   ? static_cast<wchar_t>(std::towupper(c))
                          : lowercase ? static_cast<wchar_t>(std::towlower(c))
                                      : c;
    if (accept && !accept(typed)) continue;
    clean += typed;
  }
  if (clean.empty()) return;
  if (has_selection()) erase_selection();
  if (max_length != 0) {
    const std::size_t used = length();
    if (used >= max_length) return;
    clean = first_code_points(clean, max_length - used);
  }
  text_.insert(caret_, clean);
  caret_ = anchor_ = caret_ + clean.size();
}

void TextField::paste(std::wstring_view s) {
  std::wstring clean;
  clean.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    wchar_t c = s[i];
    if (c == L'\r') {
      if (i + 1 < s.size() && s[i + 1] == L'\n') continue;  // CR LF is one break
      c = L'\n';
    }
    if (compact && (std::iswspace(c) || c == 0x3000)) continue;
    if (c == L'\t' || (c == L'\n' && !multiline)) c = L' ';
    clean += c;
  }
  insert(clean);
}

void TextField::set_composition(std::wstring_view s) {
  if (!s.empty() && has_selection() && (!accept || std::ranges::all_of(s, accept))) erase_selection();
  composition_ = s;
}

void TextField::move_to(std::size_t pos, bool extend) {
  caret_ = pos;
  if (!extend) anchor_ = pos;
}

std::wstring TextField::selected_text() const {
  if (!can_copy()) return {};
  const auto lo = std::min(caret_, anchor_), hi = std::max(caret_, anchor_);
  return text_.substr(lo, hi - lo);
}

void TextField::erase() {
  if (has_selection()) erase_selection();
}

std::pair<std::size_t, std::size_t> TextField::unit_at(std::size_t pos, int clicks) const {
  return ui::unit_at(text_, pos, clicks);
}

void TextField::press(std::size_t pos, int clicks, bool extend) {
  composition_.clear();
  clicks_ = std::clamp(clicks, 1, 3);
  const auto [lo, hi] = unit_at(pos, clicks_);
  if (extend) {
    // Shift goes on from where the selection began.
    origin_lo_ = origin_hi_ = anchor_;
    caret_ = pos < anchor_ ? lo : hi;
    return;
  }
  origin_lo_ = anchor_ = lo;
  origin_hi_ = caret_ = hi;
}

void TextField::drag_to(std::size_t pos) {
  const auto [lo, hi] = unit_at(pos, clicks_);
  if (pos < origin_lo_) {
    anchor_ = origin_hi_;
    caret_ = lo;
  } else {
    anchor_ = origin_lo_;
    caret_ = std::max(hi, origin_hi_);
  }
}

std::size_t TextField::position_at(const Gfx& gfx, const TextStyle& style, float x, float y, float wrap_width) const {
  const auto layout = wrap_width > 0 ? gfx.layout(shown(), style, wrap_width, 10000) : gfx.layout(shown(), style, 10000);
  if (!layout) return caret_;
  BOOL trailing = FALSE, inside = FALSE;
  DWRITE_HIT_TEST_METRICS m{};
  layout->HitTestPoint(x, wrap_width > 0 ? y + scroll_y_ : 0, &trailing, &inside, &m);
  std::size_t pos = m.textPosition + (trailing ? m.length : 0);
  // Clicks map to the committed text; clamp out of the composition string.
  if (pos > caret_ + composition_.size()) pos -= composition_.size();
  else if (pos > caret_) pos = caret_;
  return std::min(pos, text_.size());
}

void TextField::move_vertical(const Gfx& gfx, const TextStyle& style, float width, int dir, bool extend) {
  const auto layout = gfx.layout(text_, style, width, 10000);
  if (!layout) return;
  float x = 0, y = 0;
  DWRITE_HIT_TEST_METRICS m{};
  layout->HitTestTextPosition(static_cast<UINT32>(caret_), FALSE, &x, &y, &m);
  const float line = m.height > 0 ? m.height : style.line_height;
  BOOL trailing = FALSE, inside = FALSE;
  DWRITE_HIT_TEST_METRICS t{};
  layout->HitTestPoint(x, y + line * (dir < 0 ? -0.5f : 1.5f), &trailing, &inside, &t);
  move_to(std::min<std::size_t>(t.textPosition + (trailing ? t.length : 0), text_.size()), extend);
}

void TextField::select_all() {
  anchor_ = 0;
  caret_ = text_.size();
}

bool TextField::key_down(unsigned vk, bool ctrl, bool shift) {
  switch (vk) {
    case VK_BACK:
      if (has_selection()) erase_selection();
      else if (caret_ > 0) {
        const auto to = ctrl ? previous_word(text_, caret_) : step(text_, caret_, -1);
        text_.erase(to, caret_ - to);
        caret_ = anchor_ = to;
      }
      return true;
    case VK_DELETE:
      if (has_selection()) erase_selection();
      else if (caret_ < text_.size()) text_.erase(caret_, (ctrl ? next_word(text_, caret_) : step(text_, caret_, 1)) - caret_);
      return true;
    case VK_LEFT:
      if (ctrl) move_to(previous_word(text_, caret_), shift);
      else move_to(has_selection() && !shift ? std::min(caret_, anchor_) : step(text_, caret_, -1), shift);
      return true;
    case VK_RIGHT:
      if (ctrl) move_to(next_word(text_, caret_), shift);
      else move_to(has_selection() && !shift ? std::max(caret_, anchor_) : step(text_, caret_, 1), shift);
      return true;
    case VK_HOME:
      move_to(0, shift);
      return true;
    case VK_END:
      move_to(text_.size(), shift);
      return true;
    default:
      break;
  }
  if (!ctrl) return false;
  if (vk == 'A') {
    select_all();
    return true;
  }
  return false;
}

std::wstring TextField::shown() const {
  std::wstring base = text_;
  if (password) base.assign(base.size(), L'•');
  base.insert(caret_, password ? std::wstring(composition_.size(), L'•') : composition_);
  return base;
}

Rect TextField::paint(Painter& p, Rect box, const TextStyle& style, Color text, std::wstring_view placeholder,
                      const TextStyle& placeholder_style, Color placeholder_color, Color caret_color,
                      Color selection_color, bool focused, bool caret_on) const {
  const std::wstring display = shown();
  if (display.empty()) p.text(placeholder, box, placeholder_style, placeholder_color);

  const float caret_h = style.size * 1.25f;
  const float caret_y = style.line_height > 0 ? box.y + (box.h - caret_h) / 2 : box.y;
  Rect caret_rect{box.x, caret_y, 1.5f, caret_h};
  const auto layout = p.gfx().layout(display, style, style.wrap ? box.w : 10000, style.wrap ? 10000 : box.h);
  if (!layout) return caret_rect;

  // Text taller than its box moves up to keep the caret's line in view, and is cut at the box.
  float caret_x = 0, caret_top = 0;
  DWRITE_HIT_TEST_METRICS m{};
  layout->HitTestTextPosition(static_cast<UINT32>(caret_) + static_cast<UINT32>(composition_.size()), FALSE, &caret_x, &caret_top, &m);
  DWRITE_TEXT_METRICS metrics{};
  const float content_h = style.wrap && SUCCEEDED(layout->GetMetrics(&metrics)) ? metrics.height : 0;
  const bool scrolls = content_h > box.h + 0.5f;
  if (!scrolls) {
    scroll_y_ = 0;
  } else {
    scroll_y_ = std::clamp(scroll_y_, caret_top + m.height - box.h, caret_top);
    scroll_y_ = std::clamp(scroll_y_, 0.0f, content_h - box.h);
    p.rt()->PushAxisAlignedClip(D2D1::RectF(box.x, box.y, box.x + box.w, box.y + box.h), D2D1_ANTIALIAS_MODE_ALIASED);
  }
  const Rect clip_box = box;
  box.y -= scroll_y_;

  const UINT32 comp_len = static_cast<UINT32>(composition_.size());
  const auto range_runs = [&](std::size_t start, std::size_t length) {
    std::vector<DWRITE_HIT_TEST_METRICS> runs;
    UINT32 count = 0;
    layout->HitTestTextRange(static_cast<UINT32>(start), static_cast<UINT32>(length), 0, 0, nullptr, 0, &count);
    runs.resize(count);
    if (count == 0 || FAILED(layout->HitTestTextRange(static_cast<UINT32>(start), static_cast<UINT32>(length), 0, 0,
                                                      runs.data(), count, &count))) {
      runs.clear();
    }
    return runs;
  };

  if (focused && has_selection() && comp_len == 0) {
    const auto lo = std::min(caret_, anchor_), hi = std::max(caret_, anchor_);
    for (const auto& r : range_runs(lo, hi - lo)) {
      p.fill_rect({box.x + r.left, box.y + r.top + (r.height - caret_h) / 2, r.width, caret_h}, selection_color);
    }
  }
  if (!display.empty()) p.draw_layout(layout.get(), box.x, box.y, text);

  if (comp_len > 0) {  // the IME's uncommitted text is underlined
    for (const auto& r : range_runs(caret_, comp_len)) {
      const float y = box.y + r.top + (r.height - caret_h) / 2 + caret_h + 1;
      p.stroke_line(box.x + r.left, y, box.x + r.left + r.width, y, text, 1.5f);
    }
  }
  caret_rect.x = box.x + caret_x;
  caret_rect.y = box.y + caret_top + (m.height - caret_h) / 2;
  if (focused && caret_on && !has_selection()) p.fill_rect(caret_rect, caret_color);
  if (scrolls) p.rt()->PopAxisAlignedClip();
  // The IME's window goes by this, so it stays inside the box.
  caret_rect.y = std::clamp(caret_rect.y, clip_box.y, std::max(clip_box.y, clip_box.bottom() - caret_h));
  return caret_rect;
}

}  // namespace crpp::ui
