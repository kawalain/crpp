#include "crpp/ui/textfield.hpp"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <vector>

namespace crpp::ui {

namespace {

bool is_control(wchar_t c) { return c < 0x20 || c == 0x7F; }

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
    if (text_.size() >= max_length) return;
    clean = clean.substr(0, max_length - text_.size());
  }
  text_.insert(caret_, clean);
  caret_ = anchor_ = caret_ + clean.size();
}

void TextField::set_composition(std::wstring_view s) {
  if (!s.empty() && has_selection() && (!accept || std::ranges::all_of(s, accept))) erase_selection();
  composition_ = s;
}

void TextField::move_to(std::size_t pos, bool extend) {
  caret_ = pos;
  if (!extend) anchor_ = pos;
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
        const auto to = step(text_, caret_, -1);
        text_.erase(to, caret_ - to);
        caret_ = anchor_ = to;
      }
      return true;
    case VK_DELETE:
      if (has_selection()) erase_selection();
      else if (caret_ < text_.size()) text_.erase(caret_, step(text_, caret_, 1) - caret_);
      return true;
    case VK_LEFT:
      move_to(has_selection() && !shift ? std::min(caret_, anchor_) : step(text_, caret_, -1), shift);
      return true;
    case VK_RIGHT:
      move_to(has_selection() && !shift ? std::max(caret_, anchor_) : step(text_, caret_, 1), shift);
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
  if (vk == 'C' || vk == 'X') {
    if (!password && has_selection() && OpenClipboard(nullptr)) {
      const auto lo = std::min(caret_, anchor_), hi = std::max(caret_, anchor_);
      const std::wstring part = text_.substr(lo, hi - lo);
      if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (part.size() + 1) * sizeof(wchar_t))) {
        auto* dst = static_cast<wchar_t*>(GlobalLock(mem));
        std::copy(part.begin(), part.end(), dst);
        dst[part.size()] = L'\0';
        GlobalUnlock(mem);
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
      }
      CloseClipboard();
      if (vk == 'X') erase_selection();
    }
    return true;
  }
  if (vk == 'V') {
    if (OpenClipboard(nullptr)) {
      if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* s = static_cast<const wchar_t*>(GlobalLock(h))) {
          insert(s);
          GlobalUnlock(h);
        }
      }
      CloseClipboard();
    }
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

void TextField::click_at(const Gfx& gfx, const TextStyle& style, float x, bool extend) {
  const auto layout = gfx.layout(shown(), style, 10000);
  if (!layout) return;
  BOOL trailing = FALSE, inside = FALSE;
  DWRITE_HIT_TEST_METRICS m{};
  layout->HitTestPoint(x, 0, &trailing, &inside, &m);
  std::size_t pos = m.textPosition + (trailing ? m.length : 0);
  // Clicks map to the committed text; clamp out of the composition string.
  if (pos > caret_ + composition_.size()) pos -= composition_.size();
  else if (pos > caret_) pos = caret_;
  move_to(std::min(pos, text_.size()), extend);
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
  float x = 0, y = 0;
  DWRITE_HIT_TEST_METRICS m{};
  layout->HitTestTextPosition(static_cast<UINT32>(caret_) + comp_len, FALSE, &x, &y, &m);
  caret_rect.x = box.x + x;
  caret_rect.y = box.y + y + (m.height - caret_h) / 2;
  if (focused && caret_on && !has_selection()) p.fill_rect(caret_rect, caret_color);
  return caret_rect;
}

}  // namespace crpp::ui
