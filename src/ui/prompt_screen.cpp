#include "crpp/ui/prompt_screen.hpp"

#include <windows.h>

#include <algorithm>

namespace crpp::ui {

namespace {

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

void PromptScreen::focus(int id) {
  if (id == Field) take_focus();
}

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
  p.set_base(D2D1::Matrix3x2F::Identity());

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
  if (const skin::Screen* screen = ui_.pack_screen("prompt")) paint_pack(p, *screen, is_locked, hint_error);
}

void PromptScreen::submit() {
  if (busy_ || lock_until_ != 0) return;
  std::string text = value();
  if (!prompt_.valid || !prompt_.valid(text)) return;
  if (on_submit) on_submit(std::move(text));
}

TextStyle PromptScreen::field_style() const { return pack_field_style_; }

std::vector<MenuItem> PromptScreen::context_menu(int id, float x, float) {
  if (id == Field && lock_until_ == 0) {
    take_focus();
    field_.right_press(field_.position_at(ui_.gfx, field_style(), x - text_x_ + scroll_x_, 0));
  } else if (id != -1 || ui_.focus != &field_) {
    return {};
  }
  return ui_.field_menu(field_);
}

void PromptScreen::menu_command(MenuCommand command) {
  take_focus();
  ui_.field_command(field_, command);
}

void PromptScreen::drag(float x, float) { field_.drag_to(field_.position_at(ui_.gfx, field_style(), x - text_x_ + scroll_x_, 0)); }

void PromptScreen::click(int id, float x, float, bool shift) {
  take_focus();
  switch (id) {
    case Field:
      if (lock_until_ != 0) return;
      field_.press(field_.position_at(ui_.gfx, field_style(), x - text_x_ + scroll_x_, 0), ui_.clicks, shift);
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
