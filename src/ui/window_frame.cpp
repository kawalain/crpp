#include "crpp/ui/window_frame.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "crpp/ui/luna.hpp"

namespace crpp::ui {

namespace {

constexpr float edge = 4, corner = 12;
constexpr float button = 21, button_gap = 2, button_top = 4, button_right = 5;

Rect close_rect(float width) { return {width - button_right - button, button_top, button, button}; }
Rect minimize_rect(float width) { return {width - button_right - 2 * button - button_gap, button_top, button, button}; }

}  // namespace

WindowFrame::Zone WindowFrame::zone_at(float x, float y, float w, float h, Parts parts) {
  if (!parts.resizable) {
    if (y >= luna::caption_h || (parts.close && close_rect(w).contains(x, y))) return Zone::Client;
    return Zone::Caption;
  }
  const bool left = x < edge, right = x >= w - edge, top = y < edge, bottom = y >= h - edge;
  const bool near_left = x < corner, near_right = x >= w - corner, near_top = y < corner, near_bottom = y >= h - corner;
  if ((top && near_left) || (left && near_top)) return Zone::TopLeft;
  if ((top && near_right) || (right && near_top)) return Zone::TopRight;
  if ((bottom && near_left) || (left && near_bottom)) return Zone::BottomLeft;
  if ((bottom && near_right) || (right && near_bottom)) return Zone::BottomRight;
  if (top) return Zone::Top;
  if (bottom) return Zone::Bottom;
  if (left) return Zone::Left;
  if (right) return Zone::Right;
  if (y >= luna::caption_h || (parts.close && close_rect(w).contains(x, y)) || (parts.minimize && minimize_rect(w).contains(x, y))) return Zone::Client;
  return Zone::Caption;
}

Rect WindowFrame::content(float width, float height, bool with_menu) {
  const float top = luna::caption_h + (with_menu ? luna::menu_h : 0);
  return {luna::frame_w, top, width - 2 * luna::frame_w, height - top - luna::frame_w};
}

Rect WindowFrame::below_caption(float width, float height) { return {0, luna::caption_h, width, height - luna::caption_h}; }

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
  const Theme& t = ui_.theme;
  p.set_base(D2D1::Matrix3x2F::Identity());
  luna::window_frame(p, t, ui_.width, ui_.height, active_);
  draw_svg(p, ui_.emoji_image(""), {8, 6, 16, 16});
  const Rect minimize = minimize_rect(ui_.width), close = close_rect(ui_.width);
  const float title_end = parts_.minimize ? minimize.x : parts_.close ? close.x : ui_.width;
  luna::caption_text(p, t, {28, 0, std::max(0.0f, title_end - 32), luna::caption_h}, title_);
  if (parts_.minimize) {
    luna::caption_button(p, t, minimize, luna::CaptionGlyph::Minimize, ui_.hot(this, Minimize), ui_.pressed(this, Minimize));
    ui_.add_hit_window(this, minimize, Minimize);
  }
  if (parts_.close) {
    luna::caption_button(p, t, close, luna::CaptionGlyph::Close, ui_.hot(this, Close), ui_.pressed(this, Close));
    ui_.add_hit_window(this, close, Close);
  }
  if (!menu_shown_) {
    open_ = -1;
    return;
  }

  const Rect bar{luna::frame_w, luna::caption_h, ui_.width - 2 * luna::frame_w, luna::menu_h};
  p.fill_rect(bar, t.luna.face);
  p.fill_rect({bar.x, bar.bottom() - 1, bar.w, 1}, t.luna.edge_dark);
  const char* const names[3] = {tr().menu_file, tr().menu_tools, tr().menu_help};
  float x = bar.x;
  for (int i = 0; i < 3; ++i) {
    const TextStyle style = luna::body(p, luna::menu_h - 1, Align::Center);
    DWRITE_TEXT_METRICS m{};
    if (const auto layout = ui_.gfx.layout(widen(names[i]), style, 200)) layout->GetMetrics(&m);
    const Rect title{x, bar.y, std::ceil(m.widthIncludingTrailingWhitespace) + 14, luna::menu_h - 1};
    titles_[i] = title;
    const int id = MenuBase + i;
    // With one menu down, moving over another opens that one.
    if (open_ >= 0 && open_ != i && ui_.hot(this, id)) open_ = i;
    const bool lit = open_ == i || ui_.hot(this, id);
    if (lit) p.fill_rect(title, t.luna.select);
    p.text(names[i], title, style, lit ? t.luna.select_ink : t.luna.ink);
    ui_.add_hit_window(this, title, id);
    x = title.right();
  }
}

void WindowFrame::paint_menu(Painter& p) {
  if (open_ < 0 || !menu_shown_) return;
  const Theme& t = ui_.theme;
  p.set_base(D2D1::Matrix3x2F::Identity());
  const std::vector<Item> items = items_of(open_);
  float w = 120;
  const TextStyle style = luna::body(p, 22);
  for (const Item& item : items) {
    DWRITE_TEXT_METRICS m{};
    if (const auto layout = ui_.gfx.layout(widen(item.text), style, 400)) layout->GetMetrics(&m);
    w = std::max(w, std::ceil(m.widthIncludingTrailingWhitespace) + 40);
  }
  const float item_h = 22;
  const Rect r{titles_[open_].x, titles_[open_].bottom() + 1, w, 2 + static_cast<float>(items.size()) * item_h};
  ui_.skin().panel(p, t, r, PanelKind::Menu);
  for (std::size_t i = 0; i < items.size(); ++i) {
    const int id = ItemBase + open_ * 10 + static_cast<int>(i);
    const Rect row{r.x + 1, r.y + 1 + static_cast<float>(i) * item_h, r.w - 2, item_h};
    ui_.skin().menu_item(p, t, row, items[i].text, false, ui_.hot(this, id), false);
    ui_.add_hit_window(this, row, id);
  }
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
  } else if (id >= MenuBase) {
    open_ = open_ == id - MenuBase ? -1 : id - MenuBase;
  }
}

bool WindowFrame::escape() {
  if (open_ < 0) return false;
  open_ = -1;
  return true;
}

}  // namespace crpp::ui
