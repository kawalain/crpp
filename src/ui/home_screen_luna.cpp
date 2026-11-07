// Home in the Luna Skin: who you are, a code to enter, the Seats to go back to, and what can be
// started. It shares its state, hit ids and callbacks with the Cloud layout.
#include <algorithm>
#include <cmath>

#include "crpp/ui/home_screen.hpp"
#include "crpp/ui/luna.hpp"

namespace crpp::ui {

TextStyle HomeScreen::luna_code_style() const { return luna::body(ui_.gfx, luna::field_h); }

void HomeScreen::paint_luna(Painter& p) {
  refresh_seat(ui_.now_ms);
  const Theme& t = ui_.theme;
  const LunaColors& l = t.luna;
  const Rect c = ui_.content;
  p.set_base(D2D1::Matrix3x2F::Identity());
  p.fill_rect(c, l.face);

  // Who you are.
  const Rect header{c.x, c.y, c.w, 62};
  p.fill_down(header, l.header);
  p.fill_rect({header.x, header.bottom() - 1, header.w, 1}, l.edge_dark);
  const Rect picture{header.x + 8, header.y + 7, 48, 48};
  luna::picture_frame(p, t, picture, 0);
  draw_svg(p, ui_.emoji_image(my_emoji ? my_emoji() : std::string{}), {picture.x + 4, picture.y + 4, 40, 40});
  const char* hello = hour_ >= 5 && hour_ < 12    ? tr().home_morning
                      : hour_ >= 12 && hour_ < 18 ? tr().home_afternoon
                      : hour_ >= 18 && hour_ < 22 ? tr().home_evening
                                                  : tr().home_night;
  const float names_x = picture.right() + 8, names_w = header.right() - names_x - 6;
  p.text(nickname_, {names_x, header.y + 13, names_w, 18}, luna::bold(p, 18), l.ink);
  p.text(hello, {names_x, header.y + 32, names_w, 16}, luna::body(p, 16), l.ink_soft);

  // A code to enter.
  float y = header.bottom() + 16;
  const Rect group{c.x + 8, y, c.w - 16, 46};
  const float enter_w = luna::button_width(p, tr().home_enter);
  const Rect field{group.x + 10, group.y + 13, group.w - 20 - enter_w - 6, luna::field_h};
  luna::field(p, t, field);
  const bool focused = ui_.focus == &code_;
  code_x_ = field.x + 4;
  const TextStyle style = luna_code_style();
  ui_.caret = code_.paint(p, {code_x_, field.y, field.w - 8, field.h}, style, l.ink, widen(tr().home_code_placeholder), style, l.ink_soft, l.ink, with_alpha(l.select, 0.4f), focused, ui_.caret_on);
  ui_.add_hit(this, field, Code);
  join_rect_ = {field.right() + 6, field.y, enter_w, luna::button_h};
  luna::button(p, t, join_rect_, tr().home_enter, {.hot = ui_.hot(this, Join), .pressed = ui_.pressed(this, Join)});
  ui_.add_hit(this, join_rect_, Join);
  luna::group_box(p, t, group, tr().home_find);
  y = group.bottom() + 16;

  // What can be started.
  constexpr float tasks_h = 66;
  const Rect tasks{c.x, c.bottom() - tasks_h, c.w, tasks_h};
  p.fill_down(tasks, l.header);
  p.fill_rect({tasks.x, tasks.y, tasks.w, 1}, l.edge_dark);
  p.text(tr().home_tasks, {tasks.x + 10, tasks.y + 6, tasks.w - 20, 16}, luna::bold(p, 16), l.ink);
  const auto task = [&](float top, const char* text, int id) {
    const Rect row{tasks.x + 28, top, tasks.w - 38, 18};
    p.fill_rect({tasks.x + 14, top + 5, 8, 8}, l.link);
    luna::link(p, t, row, text, ui_.hot(this, id));
    ui_.add_hit(this, row, id);
    return row;
  };
  task(tasks.y + 24, tr().home_create, Create);
  find_rect_ = task(tasks.y + 43, tr().home_find_public, Find);

  // The Seats to go back to.
  p.text(tr().home_seats, {c.x + 10, y, c.w - 20, 16}, luna::bold(p, 16), l.ink);
  y += 18;
  const Rect list{c.x + 8, y, c.w - 16, std::max(20.0f, tasks.y - 8 - y)};
  luna::sunken(p, t, list);
  const std::vector<std::string> seats = held_seats ? held_seats() : std::vector<std::string>{};
  if (seats.empty()) {
    p.text(tr().home_seats_empty, {list.x + 6, list.y + 4, list.w - 12, list.h - 8}, luna::paragraph(p), l.ink_soft);
  } else {
    p.rt()->PushAxisAlignedClip(D2D1::RectF(list.x + 1, list.y + 1, list.right() - 1, list.bottom() - 1), D2D1_ANTIALIAS_MODE_ALIASED);
    ui_.push_hit_clip(ui_.to_window({list.x + 1, list.y + 1, list.w - 2, list.h - 2}));
    constexpr float row_h = 20;
    for (std::size_t i = 0; i < seats.size(); ++i) {
      const Rect row{list.x + 1, list.y + 1 + static_cast<float>(i) * row_h, list.w - 2, row_h};
      const int id = SeatBase + static_cast<int>(i);
      const bool hot = ui_.hot(this, id);
      if (hot) p.fill_rect(row, l.select);
      p.text(seats[i], {row.x + 5, row.y, row.w - 10, row.h}, luna::body(p, row.h), hot ? l.select_ink : l.ink);
      ui_.add_hit(this, row, id);
    }
    ui_.pop_hit_clip();
    p.rt()->PopAxisAlignedClip();
  }
}

}  // namespace crpp::ui
