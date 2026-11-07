#include "crpp/ui/prompt_screen.hpp"

#include <windows.h>

#include <algorithm>

namespace crpp::ui {

namespace {

constexpr TextStyle input_style{.size = 23, .line_height = 58};
constexpr TextStyle button_style{.size = 15, .line_height = 20, .align = Align::Center};

bool identifier_char(wchar_t c) { return (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'_' || c == L'.'; }

}  // namespace

void PromptScreen::show(Prompt prompt) {
  prompt_ = std::move(prompt);
  field_.max_length = prompt_.max_length;
  field_.password = prompt_.password;
  field_.lowercase = prompt_.lowercase;
  field_.accept = prompt_.identifier ? identifier_char : nullptr;
  field_.set_text(widen(prompt_.value));
  busy_ = false;
  lock_until_ = next_wake_ = 0;
  scroll_x_ = 0;
  seen_ = field_.text();
  valid_ = prompt_.valid && prompt_.valid(prompt_.value);
  take_focus();
}

void PromptScreen::set_value(std::string_view value) {
  field_.set_text(widen(value));
  take_focus();
}

void PromptScreen::set_error(std::string error) {
  prompt_.error = std::move(error);
  sync_with_field();
  take_focus();
}

void PromptScreen::lock(long long seconds) {
  const auto now = clock_ms();
  lock_until_ = now + static_cast<unsigned long long>(std::max(1LL, seconds)) * 1000;
  prompt_.error.clear();
  field_.clear();
  seen_.clear();
  valid_ = false;
  ui_.set_focus(nullptr, nullptr);
  plan_wake(now);
}

std::string PromptScreen::value() const { return narrow(field_.text()); }

void PromptScreen::take_focus() {
  if (lock_until_ == 0) ui_.set_focus(this, &field_);
}

// Typing goes straight to the field, so a change is noticed when it is drawn.
void PromptScreen::sync_with_field() {
  if (field_.text() == seen_) return;
  seen_ = field_.text();
  const std::string text = narrow(seen_);
  valid_ = prompt_.valid && prompt_.valid(text);
}

void PromptScreen::paint(Painter& p) {
  if (field_.text() != seen_) prompt_.error.clear();  // upstream's input handler
  sync_with_field();
  const Theme& t = ui_.theme;
  p.set_base(D2D1::Matrix3x2F::Identity());

  // .setup-card
  constexpr float pad_top = 28, pad_x = 30, pad_bottom = 26;
  const float w = std::min(440.0f, ui_.width * 0.78f);
  const float inner = w - 2 * pad_x;

  const bool is_locked = lock_until_ != 0;
  hint_shown_ = prompt_.hint;
  if (is_locked) {
    const auto left = lock_until_ > ui_.now_ms ? lock_until_ - ui_.now_ms : 0;
    if (prompt_.lock_hint) hint_shown_ = prompt_.lock_hint(static_cast<long long>((left + 999) / 1000));
  } else if (!prompt_.error.empty()) {
    hint_shown_ = prompt_.error;
  } else if (prompt_.hint_for) {
    if (std::string other = prompt_.hint_for(narrow(seen_)); !other.empty()) hint_shown_ = std::move(other);
  }
  const bool hint_error = is_locked || !prompt_.error.empty();
  const TextStyle hint_style{.size = 14, .line_height = 21, .wrap = true};
  float hint_h = 24;
  if (const auto layout = ui_.gfx.layout(widen(hint_shown_), hint_style, inner - 8)) {
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    hint_h = std::max(hint_h, m.height);
  }

  const bool has_actions = !prompt_.extra.empty() || prompt_.cancelable;
  constexpr float button_h = 36;
  float h = pad_top + pad_bottom;
  if (!prompt_.heading.empty()) h += 36 + 26;
  if (!prompt_.label.empty()) h += 22 + 10;
  h += 68 + 11 + hint_h;
  if (has_actions) h += 14 + button_h;
  if (!prompt_.link.empty()) h += 14 + button_h;

  const Rect card{(ui_.width - w) / 2, (ui_.height - h) / 2, w, h};
  if (t.dark) p.shadow_round(card, 14, 38, 12, rgb(0x000000, 0.33f));
  else p.shadow_round(card, 14, 16, 4, rgb(0x2e78aa, 0.22f));
  p.fill_round(card, 14, t.surface);

  float y = card.y + pad_top;
  const float left = card.x + pad_x;
  if (!prompt_.heading.empty()) {
    p.text(prompt_.heading, {left, y, inner, 36}, {.size = 28, .weight = 700, .line_height = 36, .align = Align::Center}, t.hl_text);
    y += 36 + 26;
  }
  if (!prompt_.label.empty()) {
    p.text(prompt_.label, {left + 4, y, inner - 4, 22}, {.size = 17, .weight = 700, .line_height = 22}, t.hl_text);
    y += 22 + 10;
  }

  // .setup-input-wrap: 20 of padding, then the text, the eye and the arrow.
  const Rect wrap{left, y, inner, 68};
  p.fill_round(wrap, 16, t.hl_pale);
  ui_.add_hit_window(this, wrap, Field);
  float text_left = wrap.x + 20;
  if (!prompt_.prefix.empty()) {
    const auto size = p.text(prompt_.prefix, {text_left, y + 5, 40, 58}, input_style, t.hl_text);
    text_left += size.width + 2;
  }
  float right = wrap.right() - 12;

  // The arrow: 44 square, lit when the value can go.
  const Rect submit_r{right - 44, y + 12, 44, 44};
  right = submit_r.x;
  const bool can_submit = valid_ && !is_locked;
  const bool submit_hot = ui_.hot(this, Submit) && can_submit && !busy_;
  const float alpha = busy_ || is_locked ? 0.6f : 1.0f;
  if (can_submit) p.fill_round(submit_r, 13, with_alpha(submit_hot ? t.hl_dark : t.hl, alpha));
  const Color arrow_ink = with_alpha(can_submit ? t.on_accent : t.hl_text_soft, alpha);
  p.push(D2D1::Matrix3x2F::Scale(30 / 44.f, 30 / 44.f) * D2D1::Matrix3x2F::Translation(submit_r.x + 7, submit_r.y + 7));
  p.stroke_geometry(ui_.gfx.path("M10 22h23M23 11l11 11-11 11"), arrow_ink, 3);
  p.pop();
  ui_.add_hit_window(this, submit_r, Submit);

  if (prompt_.password) {
    const Rect eye{right - 4 - 36, y + 16, 36, 36};
    right = eye.x;
    if (ui_.hot(this, Eye)) p.fill_round(eye, 10, t.hl_pale2);
    p.push(D2D1::Matrix3x2F::Scale(22 / 24.f, 22 / 24.f) * D2D1::Matrix3x2F::Translation(eye.x + 7, eye.y + 7));
    p.stroke_geometry(ui_.gfx.path("M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12Z"), t.hl_text, 2);
    p.stroke_ellipse(12, 12, 3, 3, t.hl_text, 2);
    if (!field_.password) p.stroke_geometry(ui_.gfx.path("M4 4l16 16"), t.hl_text, 2);
    p.pop();
    ui_.add_hit_window(this, eye, Eye);
  }

  // The input, scrolled sideways so the caret stays in view.
  const Rect box{text_left, y + 5, right - 8 - text_left, 58};
  text_x_ = box.x;
  p.rt()->PushAxisAlignedClip(D2D1::RectF(box.x, box.y, box.right(), box.bottom()), D2D1_ANTIALIAS_MODE_ALIASED);
  const bool focused = ui_.focus == &field_;
  const auto draw_field = [&] {
    return field_.paint(p, {box.x - scroll_x_, box.y, box.w + scroll_x_, box.h}, input_style, t.hl_text, widen(prompt_.placeholder),
                        input_style, t.hl_text_soft, t.hl_text, with_alpha(t.hl, 0.4f), focused, ui_.caret_on);
  };
  Rect caret = draw_field();
  const float caret_in_text = caret.x + scroll_x_ - box.x;
  float wanted = scroll_x_;
  if (caret_in_text - scroll_x_ > box.w - 2) wanted = caret_in_text - box.w + 2;
  else if (caret_in_text < scroll_x_) wanted = caret_in_text;
  if (wanted != scroll_x_) {
    scroll_x_ = wanted;
    p.fill_rect(box, t.hl_pale);
    caret = draw_field();
  }
  p.rt()->PopAxisAlignedClip();
  if (focused) ui_.caret = caret;
  y += 68;

  // .setup-hint
  y += 11;
  const Color hint_color = hint_error ? t.error_text : (t.dark ? t.hl_text_soft : rgb(0x94b8ca));
  p.text(hint_shown_, {left + 4, y, inner - 8, hint_h}, hint_style, hint_color);
  y += hint_h;

  const auto width_of = [&](const std::string& text) {
    float width = 0;
    if (const auto layout = ui_.gfx.layout(widen(text), button_style, inner)) {
      DWRITE_TEXT_METRICS m{};
      layout->GetMetrics(&m);
      width = m.widthIncludingTrailingWhitespace;
    }
    return width + 32;  // padding 8 16
  };
  const auto button = [&](const std::string& text, float x, float top, int id) {
    const Rect r{x, top, width_of(text), button_h};
    const bool hot = ui_.hot(this, id) && !busy_;
    if (hot) p.push(scale_about(r.x + r.w / 2, r.y + r.h / 2, 1.03f));
    p.fill_round(r, 10, hot ? t.hl_dark : t.hl);
    p.text(text, {r.x, r.y + 8, r.w, 20}, button_style, t.on_accent);
    if (hot) p.pop();
    ui_.add_hit_window(this, r, id);
  };
  if (has_actions) {
    y += 14;
    if (prompt_.cancelable) button("← 돌아가기", left, y, Back);
    if (!prompt_.extra.empty()) {
      float x = left + inner;
      if (!prompt_.extra2.empty()) {
        x -= width_of(prompt_.extra2);
        button(prompt_.extra2, x, y, Extra2);
        x -= 10;
      }
      x -= width_of(prompt_.extra);
      button(prompt_.extra, x, y, Extra);
    }
    y += button_h;
  }
  if (!prompt_.link.empty()) {
    y += 14;
    button(prompt_.link, left + (inner - width_of(prompt_.link)) / 2, y, Link);
  }
}

void PromptScreen::submit() {
  if (busy_ || lock_until_ != 0) return;
  std::string text = value();
  if (!prompt_.valid || !prompt_.valid(text)) return;
  if (on_submit) on_submit(std::move(text));
}

void PromptScreen::drag(float x, float) { field_.click_at(ui_.gfx, input_style, x - text_x_ + scroll_x_, true); }

void PromptScreen::click(int id, float x, float, bool shift) {
  take_focus();
  switch (id) {
    case Field:
      if (lock_until_ != 0) return;
      field_.click_at(ui_.gfx, input_style, x - text_x_ + scroll_x_, shift);
      ui_.capture = this;
      return;
    case Eye:
      field_.password = !field_.password;
      return;
    case Submit:
      submit();
      return;
    default:
      break;
  }
  if (busy_) return;
  if (id == Back && on_back) on_back();
  else if (id == Extra && on_extra) on_extra();
  else if (id == Extra2 && on_extra2) on_extra2();
  else if (id == Link && on_link) on_link();
}

void PromptScreen::enter(bool) { submit(); }

bool PromptScreen::escape() {
  if (!prompt_.cancelable || busy_) return false;
  if (on_back) on_back();
  return true;
}

Cursor PromptScreen::cursor(int id) const {
  if (id == Field) return lock_until_ != 0 ? Cursor::Arrow : Cursor::IBeam;
  if (id == Submit) return valid_ && lock_until_ == 0 && !busy_ ? Cursor::Hand : Cursor::Arrow;
  return Cursor::Hand;
}

// The hint counts minutes, so it only changes when a minute has passed.
void PromptScreen::plan_wake(unsigned long long now_ms) {
  const auto left_s = lock_until_ > now_ms ? (lock_until_ - now_ms + 999) / 1000 : 0;
  const auto until_change = left_s >= 3600 ? 3599ull * 1000 : left_s == 0 ? 0 : ((left_s + 59) / 60 - 1) * 60000;
  next_wake_ = lock_until_ - until_change;
}

bool PromptScreen::tick(unsigned long long now_ms) {
  if (lock_until_ == 0 || now_ms < next_wake_) return false;
  if (now_ms >= lock_until_) {
    lock_until_ = next_wake_ = 0;
    take_focus();
  } else {
    plan_wake(now_ms + 1);
  }
  return true;
}

}  // namespace crpp::ui
