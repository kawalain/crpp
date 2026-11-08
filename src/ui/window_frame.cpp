#include "crpp/ui/window_frame.hpp"

#include <vector>

namespace crpp::ui {

namespace {

Rect close_rect(float width, const FrameMetrics& m = {}) { return {width - m.button_right - m.button, m.button_top, m.button, m.button}; }
Rect minimize_rect(float width, const FrameMetrics& m = {}) { return {width - m.button_right - 2 * m.button - m.button_gap, m.button_top, m.button, m.button}; }

}  // namespace

WindowFrame::Zone WindowFrame::zone_at(float x, float y, float w, float h, Parts parts, const FrameMetrics& m) {
  if (!parts.resizable) {
    if (y >= m.caption_h || (parts.close && close_rect(w, m).contains(x, y))) return Zone::Client;
    return Zone::Caption;
  }
  const bool left = x < m.edge, right = x >= w - m.edge, top = y < m.edge, bottom = y >= h - m.edge;
  const bool near_left = x < m.corner, near_right = x >= w - m.corner, near_top = y < m.corner, near_bottom = y >= h - m.corner;
  if ((top && near_left) || (left && near_top)) return Zone::TopLeft;
  if ((top && near_right) || (right && near_top)) return Zone::TopRight;
  if ((bottom && near_left) || (left && near_bottom)) return Zone::BottomLeft;
  if ((bottom && near_right) || (right && near_bottom)) return Zone::BottomRight;
  if (top) return Zone::Top;
  if (bottom) return Zone::Bottom;
  if (left) return Zone::Left;
  if (right) return Zone::Right;
  if (y >= m.caption_h || (parts.close && close_rect(w, m).contains(x, y)) || (parts.minimize && minimize_rect(w, m).contains(x, y))) return Zone::Client;
  return Zone::Caption;
}

Rect WindowFrame::content(float width, float height, bool with_menu, const FrameMetrics& m) {
  const float top = m.caption_h + (with_menu ? m.menu_h : 0);
  return {m.frame_w, top, width - 2 * m.frame_w, height - top - m.frame_w};
}

Rect WindowFrame::below_caption(float width, float height, const FrameMetrics& m) { return {0, m.caption_h, width, height - m.caption_h}; }

std::vector<WindowFrame::Item> WindowFrame::items_of(int menu) {
  switch (menu) {
    case 0: {
      std::vector<Item> items;
      if (in_room_) items.push_back({tr().menu_leave, &on_leave_room});
      items.push_back({tr().menu_exit, &on_exit});
      return items;
    }
    case 1:
      return {{tr().menu_options, &on_options}};
    default:
      return {{tr().menu_changelog, &on_changelog}};
  }
}

void WindowFrame::paint(Painter& p) {
  p.set_base(D2D1::Matrix3x2F::Identity());
  if (const skin::Screen* screen = ui_.pack_screen("frame")) paint_pack(p, *screen);
}

void WindowFrame::paint_menu(Painter& p) {
  if (open_ < 0 || !menu_shown_) return;
  p.set_base(D2D1::Matrix3x2F::Identity());
  if (const skin::Screen* screen = ui_.pack_screen("frame_menu")) paint_menu_pack(p, *screen);
}

void WindowFrame::before_click(const Surface* owner, int id) {
  if (owner != this || id < MenuBase) open_ = -1;
}

void WindowFrame::click(int id, float, float, bool) {
  if (id == Minimize) {
    if (on_minimize) on_minimize();
  } else if (id == Close) {
    if (on_close) on_close();
  } else if (id >= ItemBase) {
    const int menu = (id - ItemBase) / 10, index = (id - ItemBase) % 10;
    open_ = -1;
    const std::vector<Item> items = items_of(menu);
    if (static_cast<std::size_t>(index) < items.size() && *items[static_cast<std::size_t>(index)].action) (*items[static_cast<std::size_t>(index)].action)();
  } else if (id >= MenuBase && id - MenuBase < menus) {  // a pack's hit_index can name any menu
    open_ = open_ == id - MenuBase ? -1 : id - MenuBase;
  }
}

bool WindowFrame::escape() {
  if (open_ < 0) return false;
  open_ = -1;
  return true;
}

}  // namespace crpp::ui
