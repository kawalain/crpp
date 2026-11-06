#include "crpp/ui/home_screen.hpp"

#include <windows.h>

#include <algorithm>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/home_logic.hpp"
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
  p.cloud(ui_.cloud.get(), 306.33f, 295.56f, 1.3906f, 0.09f, 6, t);
  if (ui_.hot(this, Create)) p.push(scale_about(182, 260, 1.03f));
  p.cloud(ui_.cloud.get(), cloud_x, cloud_y, 1, 0.06f, 3.742f, t);
  p.text("구름 띄우기", {0, 318, 458, 110}, {.size = 44, .line_height = 110, .align = Align::Center}, t.hl_text);
  p.pop();
  ui_.add_hit(this, {-66, 100, 500, 330}, Create);

  // Greeting.
  const char* hello = hour_ >= 5 && hour_ < 12    ? "좋은 아침이에요"
                      : hour_ >= 12 && hour_ < 18 ? "좋은 오후예요"
                      : hour_ >= 18 && hour_ < 22 ? "좋은 저녁이에요"
                                                  : "좋은 밤이에요";
  const TextStyle greeting{.size = 44, .line_height = 44, .align = Align::Center};
  p.text(hello, {440, 108, 420, 44}, greeting, rgb(0xFFFFFF));
  p.text(nickname_ + " 님!", {440, 108 + 44 + 13, 420, 44}, greeting, rgb(0xFFFFFF));

  // Find a cloud.
  p.text("구름 찾기", {470, 452, 400, 60}, {.size = 40, .line_height = 60}, t.hl_text);

  const Rect entry{469, 522, 279, 80};
  const bool focused = ui_.focus == &code_;
  p.push(focused ? scale_about(469 + 279 / 2.f, 522 + 40, 1.012f) : D2D1::Matrix3x2F::Identity());
  p.fill_round(entry, 18, t.entry);
  const Color faint = with_alpha(t.dark ? t.hl_text_soft : rgb(0xFFFFFF), t.dark ? 1.0f : 0.85f);
  const Color input_text = t.dark ? t.hl_text : t.on_accent;
  const Rect field{entry.x + 18, entry.y + 10, 205, 60};
  ui_.caret = ui_.to_window(code_.paint(p, field, code_style, input_text, L"코드 입력", {.size = 26, .line_height = 60},
                                        faint, input_text, with_alpha(t.hl, 0.4f), focused, ui_.caret_on));

  // Enter arrow.
  const Rect enter{entry.x + 221, entry.y + 10, 52, 60};
  const bool enter_hot = ui_.hot(this, Join);
  if (enter_hot) p.fill_round(enter, 13, mix(t.entry, rgb(0xFFFFFF), 40));
  const Color enter_color = enter_hot ? t.hl_text : (t.dark ? t.hl_text_soft : faint);
  {
    const auto arrow = ui_.gfx.parse_path("M10 22h23M23 11l11 11-11 11");
    p.push(D2D1::Matrix3x2F::Scale(32 / 44.f, 32 / 44.f) *
           D2D1::Matrix3x2F::Translation(enter.x + 26 - 16, enter.y + 30 - 16));
    p.stroke_geometry(arrow.get(), enter_color, 3);
    p.pop();
  }
  p.pop();
  ui_.add_hit(this, entry, Code);
  ui_.add_hit(this, enter, Join);

  // Public room search button.
  const Rect search{759, 522, 92, 80};
  const bool search_hot = ui_.hot(this, Find);
  if (search_hot) p.push(scale_about(search.x + search.w / 2, search.y + search.h / 2, 1.05f));
  // A held Seat lights the button (.search-button.returning).
  p.fill_round(search, 18, search_hot ? t.hl_dark : returning() ? t.hl : t.entry);
  {
    const Color rest = t.dark ? (returning() ? t.on_accent : t.hl_text_soft) : faint;
    const Color icon = search_hot ? t.on_accent : rest;
    p.push(D2D1::Matrix3x2F::Scale(42 / 44.f, 42 / 44.f) *
           D2D1::Matrix3x2F::Translation(search.x + 46 - 21, search.y + 40 - 21));
    p.stroke_ellipse(19, 19, 11, 11, icon, 3.2f);
    p.stroke_line(27.5f, 27.5f, 37, 37, icon, 3.2f);
    p.pop();
  }
  if (search_hot) p.pop();
  ui_.add_hit(this, search, Find);
}

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
  ui_.cloud->FillContainsPoint(D2D1::Point2F(ui_.to_stage_x(x), ui_.to_stage_y(y)),
                               D2D1::Matrix3x2F::Translation(cloud_x, cloud_y), &inside);
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
  refresh_seat(GetTickCount64());
  if (home::find_step(returning()) == home::FindStep::AskReturn) {
    return_open_ = true;
    return_opened_at_ = GetTickCount64();
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
  } else {
    ui_.toasts->toast(std::move(text));
  }
}

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
  return return_open_ && static_cast<float>(now - return_opened_at_) < modal_pop_ms;
}

bool HomeScreen::animating() const { return ui_.animations && moving(GetTickCount64()); }

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
  return changed;
}

unsigned long long HomeScreen::wake_at() const { return seat_wake_; }

}  // namespace crpp::ui
