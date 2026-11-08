#include "crpp/ui/home_screen.hpp"

#include <algorithm>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/home_logic.hpp"
#include "crpp/ui/toasts.hpp"

namespace crpp::ui {

HomeScreen::HomeScreen(Ui& ui) : Surface(ui) {
  code_.max_length = 6;
  code_.uppercase = true;
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
  seat_wake_ = left ? now + *left : 0;
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
  if (ui_.skin().scales_stage() && home::find_step(returning()) == home::FindStep::AskReturn) {
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

void HomeScreen::drag(float x, float) { code_.click_at(ui_.gfx, field_style(), ui_.to_stage_x(x) - code_x_, true); }

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
      code_.click_at(ui_.gfx, field_style(), ui_.to_stage_x(x) - code_x_, shift);
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
        const std::vector<std::string> seats = held_seats();
        const auto index = static_cast<std::size_t>(id - SeatBase);
        if (index < seats.size()) {
          last_action_ = Create;  // the list is what tried, so a failure goes to the top
          on_join(seats[index]);
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
  if (seat_wake_ != 0 && now_ms >= seat_wake_) {
    refresh_seat(now_ms);
    changed = true;
  }
  if (tip_muted_until_ != 0 && now_ms >= tip_muted_until_) {
    tip_muted_until_ = 0;
    changed = true;
  }
  return changed;
}

unsigned long long HomeScreen::wake_at() const {
  if (seat_wake_ == 0 || tip_muted_until_ == 0) return seat_wake_ + tip_muted_until_;
  return std::min(seat_wake_, tip_muted_until_);
}

}  // namespace crpp::ui
