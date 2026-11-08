#include "crpp/ui/window_frame.hpp"

#include <windows.h>

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

const char* WindowFrame::title_of(int menu) {
  switch (menu) {
    case 0: return tr().menu_file;
    case 1: return tr().menu_tools;
    default: return tr().menu_help;
  }
}

void WindowFrame::enter_menus() {
  if (!menu_shown_) return;
  leave_menus();
  bar_ = 0;
}

bool WindowFrame::open_by_key(char key) {
  if (!menu_shown_ || key == 0) return false;
  for (int i = 0; i < menus; ++i) {
    if (locale::access_key(title_of(i)) == key) {
      open_menu(i, 0);
      return true;
    }
  }
  return false;
}

void WindowFrame::run(int menu, int index) {
  leave_menus();
  const std::vector<Item> items = items_of(menu);
  if (index >= 0 && static_cast<std::size_t>(index) < items.size() && *items[static_cast<std::size_t>(index)].action) {
    (*items[static_cast<std::size_t>(index)].action)();
  }
}

bool WindowFrame::menu_key(unsigned vk) {
  const int at = open_ >= 0 ? open_ : bar_ >= 0 ? bar_ : 0;
  const int count = open_ >= 0 ? static_cast<int>(items_of(open_).size()) : 0;
  switch (vk) {
    case VK_ESCAPE:
      if (open_ >= 0) {
        bar_ = open_;
        open_ = item_ = -1;
      } else {
        leave_menus();
      }
      return true;
    case VK_LEFT:
    case VK_RIGHT: {
      const int next = (at + (vk == VK_RIGHT ? 1 : menus - 1)) % menus;
      if (open_ >= 0) open_menu(next, 0);
      else bar_ = next;
      return true;
    }
    case VK_DOWN:
    case VK_UP:
      if (open_ < 0) {
        open_menu(at, 0);
        if (vk == VK_UP) item_ = static_cast<int>(items_of(at).size()) - 1;
      } else if (count > 0) {
        item_ = item_ < 0 ? (vk == VK_DOWN ? 0 : count - 1) : (item_ + (vk == VK_DOWN ? 1 : count - 1)) % count;
      }
      return true;
    case VK_RETURN:
      if (open_ < 0) open_menu(at, 0);
      else if (item_ >= 0) run(open_, item_);
      return true;
    default:
      break;
  }
  const bool letter = (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9');
  if (!letter) return false;
  if (open_ < 0) {
    open_by_key(static_cast<char>(vk));
    return true;
  }
  const std::vector<Item> items = items_of(open_);
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (locale::access_key(items[i].text) == static_cast<char>(vk)) {
      run(open_, static_cast<int>(i));
      break;
    }
  }
  return true;
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
  // A click leaves the keyboard's place in the menus; a menu it opens still takes the keys.
  bar_ = item_ = -1;
  if (owner != this || id < MenuBase) open_ = -1;
}

void WindowFrame::click(int id, float, float, bool) {
  if (id == Minimize) {
    if (on_minimize) on_minimize();
  } else if (id == Close) {
    if (on_close) on_close();
  } else if (id >= ItemBase) {
    run((id - ItemBase) / 10, (id - ItemBase) % 10);
  } else if (id >= MenuBase && id - MenuBase < menus) {  // a pack's hit_index can name any menu
    open_ = open_ == id - MenuBase ? -1 : id - MenuBase;
  }
}

bool WindowFrame::escape() {
  if (open_ < 0 && bar_ < 0) return false;
  leave_menus();
  return true;
}

}  // namespace crpp::ui
