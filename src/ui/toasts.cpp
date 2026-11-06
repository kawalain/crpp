#include "crpp/ui/toasts.hpp"

#include <windows.h>

#include <algorithm>

#include "crpp/ui/controls.hpp"

namespace crpp::ui {

namespace {
constexpr float toast_ms = 180;   // .toast transition
constexpr float bubble_ms = 150;  // .btn-notice transition
}  // namespace

void Toasts::toast(std::string text, unsigned long long duration_ms) { toast(std::move(text), {}, nullptr, duration_ms); }

void Toasts::toast(std::string text, std::string action, std::function<void()> on_action, unsigned long long duration_ms) {
  const auto now = clock_ms();
  bubble_ = {};
  // A toast already up changes its words without sliding in again.
  const bool up = toast_.active() && (toast_.hide_at == 0 || now < toast_.hide_at);
  toast_ = {std::move(text), up ? toast_.shown_at : now, duration_ms ? now + duration_ms : 0};
  action_ = std::move(action);
  on_action_ = std::move(on_action);
}

void Toasts::bubble(Rect anchor, std::string text, float gap, unsigned long long duration_ms) {
  const auto now = clock_ms();
  toast_ = {};
  on_action_ = nullptr;
  bubble_ = {std::move(text), now, duration_ms ? now + duration_ms : 0};
  anchor_ = anchor;
  gap_ = gap;
}

void Toasts::clear() {
  toast_ = {};
  bubble_ = {};
  on_action_ = nullptr;
}

float Toasts::visibility(const Message& m, float duration_ms) const {
  if (!ui_.animations) return 1;
  if (m.hide_at && ui_.now_ms >= m.hide_at) return 1 - transition(ui_, m.hide_at, duration_ms);
  return transition(ui_, m.shown_at, duration_ms);
}

void Toasts::paint(Painter& p) {
  p.set_base(D2D1::Matrix3x2F::Identity());
  if (toast_.active()) paint_toast(p);
  if (bubble_.active()) paint_bubble(p);
}

void Toasts::paint_toast(Painter& p) {
  const Theme& t = ui_.theme;
  constexpr float pad_x = 18, pad_y = 11, gap = 16, action_pad_x = 13, action_h = 32;
  const float max_w = ui_.width - 32;
  const TextStyle body{.size = 15, .weight = 700, .line_height = 22, .wrap = true};
  const TextStyle action_style{.size = 15, .weight = 700, .line_height = action_h, .align = Align::Center};
  float action_w = 0;
  if (!action_.empty()) {
    DWRITE_TEXT_METRICS m{};
    if (const auto layout = ui_.gfx.layout(widen(action_), action_style, max_w)) layout->GetMetrics(&m);
    action_w = m.widthIncludingTrailingWhitespace + 2 * action_pad_x;
  }
  const float text_max = std::max(40.0f, max_w - 2 * pad_x - (action_w > 0 ? action_w + gap : 0));
  DWRITE_TEXT_METRICS size{};
  if (const auto layout = ui_.gfx.layout(widen(toast_.text), body, text_max)) layout->GetMetrics(&size);
  const float inner_h = std::max(size.height, action_w > 0 ? action_h : 0.0f);
  const float w = size.widthIncludingTrailingWhitespace + 2 * pad_x + (action_w > 0 ? action_w + gap : 0);
  const float h = inner_h + 2 * pad_y;
  const float shown = visibility(toast_, toast_ms);
  // Slides from above the window to 8 below its top (translate(-50%, -150%)).
  const float y = 8 - (h * 1.5f + 8) * (1 - shown);
  const Rect card{(ui_.width - w) / 2, y, w, h};
  p.shadow_round(card, 14, 16, 4, rgb(0x2e78aa, 0.22f * shown));
  p.fill_round(card, 14, with_alpha(t.surface, shown));
  p.text(toast_.text, {card.x + pad_x, card.y + (h - size.height) / 2, size.widthIncludingTrailingWhitespace + 1, size.height},
         body, with_alpha(t.hl_text, shown));
  if (action_w > 0) {
    const Rect button{card.right() - pad_x - action_w, card.y + (h - action_h) / 2, action_w, action_h};
    p.fill_round(button, 8, with_alpha(ui_.hot(this, Action) ? t.hl_dark : t.hl, shown));
    p.text(action_, button, action_style, with_alpha(t.on_accent, shown));
    ui_.add_hit_window(this, button, Action);
  }
}

void Toasts::paint_bubble(Painter& p) {
  const Theme& t = ui_.theme;
  constexpr float pad_x = 15, pad_y = 5;
  const TextStyle body{.size = 16, .weight = 700, .line_height = 22, .align = Align::Center, .wrap = true};
  DWRITE_TEXT_METRICS size{};
  if (const auto layout = ui_.gfx.layout(widen(bubble_.text), body, 420 - 2 * pad_x)) layout->GetMetrics(&size);
  const float w = size.widthIncludingTrailingWhitespace + 2 * pad_x, h = size.height + 2 * pad_y;
  const float shown = visibility(bubble_, bubble_ms);
  const Rect r{anchor_.right() - w, anchor_.y - gap_ - h + 4 * (1 - shown), w, h};
  p.fill_round(r, 11, with_alpha(t.hl, shown));
  p.text(bubble_.text, {r.x + pad_x, r.y + pad_y, w - 2 * pad_x + 1, size.height}, body, with_alpha(t.on_accent, shown));
}

void Toasts::click(int id, float, float, bool) {
  if (id != Action) return;
  const auto action = std::move(on_action_);
  on_action_ = nullptr;
  toast_ = {};
  if (action) action();
}

bool Toasts::moving(const Message& m, float duration_ms, unsigned long long now) const {
  if (!m.active()) return false;
  if (static_cast<float>(now - m.shown_at) < duration_ms) return true;
  return m.hide_at && now >= m.hide_at;
}

bool Toasts::animating() const {
  if (!ui_.animations) return false;
  const auto now = clock_ms();
  return moving(toast_, toast_ms, now) || moving(bubble_, bubble_ms, now);
}

bool Toasts::tick(unsigned long long now_ms) {
  const bool was_moving = moving(toast_, toast_ms, now_ms) || moving(bubble_, bubble_ms, now_ms);
  const auto expired = [&](const Message& m, float duration_ms) {
    return m.active() && m.hide_at && static_cast<float>(static_cast<long long>(now_ms - m.hide_at)) >= duration_ms &&
           now_ms >= m.hide_at;
  };
  bool changed = was_moving;
  if (expired(toast_, toast_ms)) {
    toast_ = {};
    on_action_ = nullptr;
    changed = true;
  }
  if (expired(bubble_, bubble_ms)) {
    bubble_ = {};
    changed = true;
  }
  return changed;
}

unsigned long long Toasts::wake_at() const {
  unsigned long long at = 0;
  for (const Message* m : {&toast_, &bubble_}) {
    if (m->active() && m->hide_at && (at == 0 || m->hide_at < at)) at = m->hide_at;
  }
  return at;
}

}  // namespace crpp::ui
