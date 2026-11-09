#include "crpp/ui/home_screen.hpp"

#include <algorithm>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/home_logic.hpp"
#include "crpp/ui/toasts.hpp"

namespace crpp::ui {

HomeScreen::HomeScreen(Ui& ui) : Surface(ui) {
  code_.max_length = 6;
  code_.uppercase = true;
  code_.compact = true;
}

TextStyle HomeScreen::field_style() const { return pack_code_style_; }

void HomeScreen::paint(Painter& p) {
  if (const skin::Screen* screen = ui_.pack_screen("home")) paint_pack(p, *screen);
}

bool HomeScreen::tip_wanted(unsigned long long now) const { return ui_.hot(this, Find) && now >= tip_muted_until_; }

void HomeScreen::paint_return(Painter& p) {
  if (!return_open_) return;
  if (const skin::Screen* screen = ui_.pack_screen("home_return")) paint_return_pack(p, *screen);
}

void HomeScreen::refresh_seat(unsigned long long now) {
  const auto left = held_seat_ms ? held_seat_ms() : std::nullopt;
  seat_ends_ = left ? now + *left : 0;
}

void HomeScreen::note_time_left(std::uint64_t left_ms) {
  // The minutes shown are rounded up, so they change when the rest of a minute has gone.
  const std::uint64_t to_change = left_ms % 60000 == 0 ? 60000 : left_ms % 60000;
  if (next_minute_ms_ == 0 || to_change < next_minute_ms_) next_minute_ms_ = to_change;
}

std::string seat_time_left(std::uint64_t left_ms) {
  return locale::fill(tr().home_seat_left, {std::to_string(std::max<std::uint64_t>(1, (left_ms + 59999) / 60000))});
}

void HomeScreen::join() {
  if (is_busy()) return;
  last_action_ = Join;
  const auto code = home::normalize_code(code_.text());
  if (!code) {
    show_error(tr().home_code_length);
    return;
  }
  if (on_join) on_join(narrow(*code));
}

void HomeScreen::find() {
  if (is_busy()) return;
  refresh_seat(clock_ms());
  // A Skin that lists the Seats has no need to ask.
  if (ui_.scales_stage() && home::find_step(returning()) == home::FindStep::AskReturn) {
    return_open_ = true;
    return;
  }
  last_action_ = Find;
  if (on_find_public) on_find_public();
}

void HomeScreen::left_home() {
  return_open_ = false;
  code_.clear();
}

void HomeScreen::show_error(std::string text) {
  if (last_action_ == Join) {
    ui_.toasts->bubble(ui_.to_window(join_rect_), std::move(text), 18);
  } else if (last_action_ == Find) {
    ui_.toasts->bubble(ui_.to_window(find_rect_), std::move(text));
    tip_muted_until_ = clock_ms() + Toasts::default_ms + 200;  // .notifying > .tip
  } else {
    ui_.toasts->toast(std::move(text));
  }
}

void HomeScreen::focus(int id) {
  if (id == Code) ui_.set_focus(this, &code_);
}

std::vector<MenuItem> HomeScreen::context_menu(int id, float x, float) {
  if (id == Code) {
    ui_.set_focus(this, &code_);
    code_.right_press(code_.position_at(ui_.gfx, field_style(), ui_.to_stage_x(x) - code_x_, 0));
  } else if (id != -1 || ui_.focus != &code_) {
    return {};
  }
  return ui_.field_menu(code_);
}

void HomeScreen::menu_command(MenuCommand command) {
  ui_.set_focus(this, &code_);
  ui_.field_command(code_, command);
}

void HomeScreen::drag(float x, float) { code_.drag_to(code_.position_at(ui_.gfx, field_style(), ui_.to_stage_x(x) - code_x_, 0)); }

void HomeScreen::click(int id, float x, float, bool shift) {
  switch (id) {
    case Create:
      if (is_busy()) break;
      last_action_ = Create;
      if (on_create) on_create();
      break;
    case Join:
      ui_.set_focus(this, &code_);
      join();
      break;
    case Code:
      ui_.set_focus(this, &code_);
      code_.press(code_.position_at(ui_.gfx, field_style(), ui_.to_stage_x(x) - code_x_, 0), ui_.clicks, shift);
      ui_.capture = this;
      break;
    case Find:
      find();
      break;
    case ReturnBackdrop:
      return_open_ = false;
      break;
    case ReturnConfirm:
      return_open_ = false;
      if (is_busy()) break;
      last_action_ = Join;
      if (on_return) on_return();
      break;
    case ReturnRandom:
      return_open_ = false;
      if (is_busy()) break;
      last_action_ = Find;
      if (on_find_public) on_find_public();
      break;
    default:
      if (id >= SeatBase && !is_busy() && held_seats && on_join) {
        const std::vector<HeldSeatShown> seats = held_seats();
        const auto index = static_cast<std::size_t>(id - SeatBase);
        if (index < seats.size()) {
          last_action_ = Create;  // the list is what tried, so a failure goes to the top
          on_join(seats[index].code);
        }
      }
      break;
  }
}

void HomeScreen::enter(bool) { join(); }

bool HomeScreen::escape() {
  if (!return_open_) return false;
  return_open_ = false;
  return true;
}

Cursor HomeScreen::cursor(int id) const {
  if (id == ReturnBackdrop || id == ReturnCard) return Cursor::Arrow;
  return id == Code ? Cursor::IBeam : Cursor::Hand;
}

bool HomeScreen::tick(unsigned long long now_ms) {
  bool changed = false;
  // Here and not only in paint: a hidden window never paints, and must not be woken
  // again at once for the same moment.
  if (seat_ends_ != 0 && now_ms >= seat_ends_) {
    refresh_seat(now_ms);
    changed = true;
  }
  // Drawn again, it says the next minute; hidden, it is not woken again for it.
  if (minute_wake_ != 0 && now_ms >= minute_wake_) {
    minute_wake_ = 0;
    changed = true;
  }
  if (tip_muted_until_ != 0 && now_ms >= tip_muted_until_) {
    tip_muted_until_ = 0;
    changed = true;
  }
  return changed;
}

unsigned long long HomeScreen::wake_at() const {
  unsigned long long soonest = 0;
  for (const unsigned long long at : {seat_ends_, minute_wake_, tip_muted_until_}) {
    if (at != 0 && (soonest == 0 || at < soonest)) soonest = at;
  }
  return soonest;
}

}  // namespace crpp::ui
