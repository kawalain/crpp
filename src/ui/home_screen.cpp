#include "crpp/ui/home_screen.hpp"

#include <windows.h>

#include <algorithm>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/home_logic.hpp"
#include "crpp/ui/hover.hpp"
#include "crpp/ui/toasts.hpp"

namespace crpp::ui {

namespace {

// The create cloud art is drawn at this stage position (main.js HOME_CLOUD_Y).
constexpr float cloud_x = -66.0667f;
constexpr float cloud_y = 100.9f;

constexpr TextStyle code_style{.size = 34, .line_height = 60, .letter_spacing = 34 * 0.08f};

}  // namespace

HomeScreen::HomeScreen(Ui& ui) : Surface(ui) {
  code_.max_length = 6;
  code_.uppercase = true;
}

void HomeScreen::paint(Painter& p) {
  refresh_seat(ui_.now_ms);
  const Theme& t = ui_.theme;
  // Sky: the large cloud at lower right, then the create cloud at upper left.
  p.cloud(ui_.cloud(), 306.33f, 295.56f, 1.3906f, 0.09f, 6, t);
  const unsigned long long now = ui_.now_ms;
  const bool animate = ui_.animations;
  create_hover_.aim(ui_.hot(this, Create), now, animate);
  p.push(scale_about(182, 260, 1 + 0.03f * create_hover_.value(now)));
  p.cloud(ui_.cloud(), cloud_x, cloud_y, 1, 0.06f, 3.742f, t);
  p.text("구름 띄우기", {0, 318, 458, 110}, {.size = 44, .line_height = 110, .align = Align::Center}, t.hl_text);
  p.pop();
  ui_.add_hit(this, {-66, 100, 500, 330}, Create);

  // Greeting.
  const char* hello = hour_ >= 5 && hour_ < 12    ? "좋은 아침이에요"
                      : hour_ >= 12 && hour_ < 18 ? "좋은 오후예요"
                      : hour_ >= 18 && hour_ < 22 ? "좋은 저녁이에요"
                                                  : "좋은 밤이에요";
  const TextStyle greeting{.size = 44, .line_height = 44, .align = Align::Center};
  p.text(hello, {440, 108, 420, 44}, greeting, t.dark ? t.hl_text_dark : rgb(0xFFFFFF));
  p.text(nickname_ + " 님!", {440, 108 + 44 + 13, 420, 44}, greeting, t.dark ? t.hl_text_dark : rgb(0xFFFFFF));

  // Find a cloud.
  p.text("구름 찾기", {470, 452, 400, 60}, {.size = 40, .line_height = 60}, t.hl_text);

  const Rect entry{469, 522, 279, 80};
  const bool focused = ui_.focus == &code_;
  entry_focus_.aim(focused, now, animate);
  const D2D1_MATRIX_3X2_F entry_scale = scale_about(469 + 279 / 2.f, 522 + 40, 1 + 0.012f * entry_focus_.value(now));
  p.push(entry_scale);
  p.fill_round(entry, 18, t.entry);
  const Color faint = with_alpha(t.dark ? t.hl_text_soft : rgb(0xFFFFFF), t.dark ? 1.0f : 0.85f);
  const Color input_text = t.dark ? t.hl_text : t.on_accent;
  const Rect field{entry.x + 18, entry.y + 10, 205, 60};
  ui_.caret = ui_.to_window(code_.paint(p, field, code_style, input_text, L"코드 입력", {.size = 26, .line_height = 60},
                                        faint, input_text, with_alpha(t.hl, 0.4f), focused, ui_.caret_on));

  // Enter arrow.
  const Rect enter{entry.x + 221, entry.y + 10, 52, 60};
  enter_hover_.aim(ui_.hot(this, Join), now, animate);
  const float enter_hover = enter_hover_.value(now);
  if (enter_hover > 0) {
    p.fill_round(enter, 13, with_alpha(t.dark ? t.hl_soft : mix(t.entry, rgb(0xFFFFFF), 40), enter_hover));
  }
  const Color enter_color = mix(t.dark ? t.hl_text_dark : t.hl_text, t.dark ? t.hl_text_soft : faint, enter_hover * 100);
  {
    const auto arrow = ui_.gfx.parse_path("M10 22h23M23 11l11 11-11 11");
    p.push(D2D1::Matrix3x2F::Scale(32 / 44.f, 32 / 44.f) *
           D2D1::Matrix3x2F::Translation(enter.x + 26 - 16, enter.y + 30 - 16) * entry_scale);
    p.stroke_geometry(arrow.get(), enter_color, 3);
    p.pop();
  }
  p.pop();
  ui_.add_hit(this, entry, Code);
  ui_.add_hit(this, enter, Join);

  // Public room search button.
  const Rect search{759, 522, 92, 80};
  const bool search_hot = ui_.hot(this, Find);
  search_color_.aim(search_hot, now, animate);
  search_scale_.aim(search_hot, now, animate);
  seat_light_.aim(returning(), now, animate);
  search_tip_.aim(tip_wanted(now), now, animate);
  const D2D1_MATRIX_3X2_F button_scale =
      scale_about(search.x + search.w / 2, search.y + search.h / 2, 1 + 0.05f * search_scale_.value(now));
  const float search_hover = search_color_.value(now) * 100, light = seat_light_.value(now) * 100;
  p.push(button_scale);
  // A held Seat lights the button (.search-button.returning).
  p.fill_round(search, 18, mix(t.hl_dark, mix(t.hl, t.entry, light), search_hover));
  {
    const Color rest = t.dark ? mix(t.on_accent, t.hl_text_soft, light) : faint;
    const Color icon = mix(t.on_accent, rest, search_hover);
    p.push(D2D1::Matrix3x2F::Scale(42 / 44.f, 42 / 44.f) *
           D2D1::Matrix3x2F::Translation(search.x + 46 - 21, search.y + 40 - 21) * button_scale);
    p.stroke_ellipse(19, 19, 11, 11, icon, 3.2f);
    p.stroke_line(27.5f, 27.5f, 37, 37, icon, 3.2f);
    p.pop();
  }
  if (const float shown = search_tip_.value(now); shown > 0) {
    // The label is the button's child, so it scales with it.
    constexpr const char* label = "공개된 구름 찾기";
    const float w = tip_width(ui_, label), h = tip_height();
    p.push(button_scale);
    paint_tip(p, {search.x + (search.w - w) / 2, search.y - 8 - h + 4 * (1 - shown), w, h}, label, t.hl, t.on_accent, shown);
    p.pop();
  }
  ui_.add_hit(this, search, Find);
}

bool HomeScreen::tip_wanted(unsigned long long now) const { return ui_.hot(this, Find) && now >= tip_muted_until_; }

void HomeScreen::paint_return(Painter& p) {
  if (!return_open_) return;
  constexpr const char* question = "이전에 참가했던 구름으로\n돌아갈까요?";
  constexpr const char* confirm = "돌아가기";
  constexpr const char* random = "무작위 참가";
  // .modal-card p, .modal-card button: a button grows past its minimum with its label.
  constexpr float pad_top = 24, pad_x = 25, pad_bottom = 20, gap = 20, button_h = 38, button_gap = 10;
  const TextStyle body{.size = 18, .weight = 700, .line_height = 27, .align = Align::Center, .wrap = true};
  const auto button_width = [&](const char* label) {
    DWRITE_TEXT_METRICS m{};
    if (const auto layout = ui_.gfx.layout(widen(label), {.size = 16, .weight = 700}, 400)) layout->GetMetrics(&m);
    return std::max(96.0f, m.widthIncludingTrailingWhitespace + 28);
  };
  const float w = std::min(320.0f, ui_.width - 40);
  const float text_w = w - 2 * pad_x;
  DWRITE_TEXT_METRICS size{};
  if (const auto layout = ui_.gfx.layout(widen(question), body, text_w)) layout->GetMetrics(&size);
  const float h = pad_top + size.height + gap + button_h + pad_bottom;

  const float shown = transition(ui_, return_opened_at_, modal_pop_ms);
  const Rect card = paint_modal(ui_, this, p, w, h, shown, ReturnBackdrop, ReturnCard);
  p.text(question, {card.x + pad_x, card.y + pad_top, text_w, size.height}, body, with_alpha(ui_.theme.hl_text, shown));

  const float confirm_w = button_width(confirm), random_w = button_width(random);
  const float x = card.x + (w - confirm_w - button_gap - random_w) / 2;
  const float y = card.bottom() - pad_bottom - button_h;
  paint_modal_button(ui_, this, p, {x, y, confirm_w, button_h}, confirm, true, shown, ReturnConfirm);
  paint_modal_button(ui_, this, p, {x + confirm_w + button_gap, y, random_w, button_h}, random, false, shown, ReturnRandom);
}

void HomeScreen::refresh_seat(unsigned long long now) {
  const auto left = held_seat_ms ? held_seat_ms() : std::nullopt;
  seat_wake_ = left ? now + *left : 0;
}

bool HomeScreen::accepts(int id, float x, float y) const {
  if (id != Create) return true;
  // The click area is the cloud's shape, not a rectangle.
  BOOL inside = FALSE;
  if (ID2D1PathGeometry* const shape = ui_.cloud()) {
    shape->FillContainsPoint(D2D1::Point2F(ui_.to_stage_x(x), ui_.to_stage_y(y)),
                             D2D1::Matrix3x2F::Translation(cloud_x, cloud_y), &inside);
  }
  return inside != FALSE;
}

void HomeScreen::join() {
  if (is_busy()) return;
  last_action_ = Join;
  const auto code = home::normalize_code(code_.text());
  if (!code) {
    show_error(home::code_length_error);
    return;
  }
  if (on_join) on_join(narrow(*code));
}

void HomeScreen::find() {
  if (is_busy()) return;
  refresh_seat(clock_ms());
  if (home::find_step(returning()) == home::FindStep::AskReturn) {
    return_open_ = true;
    return_opened_at_ = clock_ms();
    was_moving_ = true;
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
    ui_.toasts->bubble(ui_.to_window({469 + 221, 522 + 10, 52, 60}), std::move(text), 18);
  } else if (last_action_ == Find) {
    ui_.toasts->bubble(ui_.to_window({759, 522, 92, 80}), std::move(text));
    tip_muted_until_ = clock_ms() + Toasts::default_ms + 200;  // .notifying > .tip
  } else {
    ui_.toasts->toast(std::move(text));
  }
}

void HomeScreen::drag(float x, float) { code_.click_at(ui_.gfx, code_style, ui_.to_stage_x(x) - (469 + 18), true); }

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
      code_.click_at(ui_.gfx, code_style, ui_.to_stage_x(x) - (469 + 18), shift);
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

bool HomeScreen::moving(unsigned long long now) const {
  return (return_open_ && static_cast<float>(now - return_opened_at_) < modal_pop_ms) ||
         create_hover_.moving(ui_.hot(this, Create), now) || entry_focus_.moving(ui_.focus == &code_, now) ||
         enter_hover_.moving(ui_.hot(this, Join), now) || search_color_.moving(ui_.hot(this, Find), now) ||
         search_scale_.moving(ui_.hot(this, Find), now) || seat_light_.moving(returning(), now) ||
         search_tip_.moving(tip_wanted(now), now);
}

bool HomeScreen::animating() const { return ui_.animations && moving(clock_ms()); }

bool HomeScreen::tick(unsigned long long now_ms) {
  const bool is_moving = ui_.animations && moving(now_ms);
  bool changed = is_moving || was_moving_;
  was_moving_ = is_moving;
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
