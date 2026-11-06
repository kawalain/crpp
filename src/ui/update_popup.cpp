#include "crpp/ui/update_popup.hpp"

#include <algorithm>

#include "crpp/ui/controls.hpp"

namespace crpp::ui {

void UpdatePopup::show_available() {
  state_ = State::Available;
  percent_ = 0;
}

void UpdatePopup::show_progress(int percent) {
  state_ = State::Busy;
  percent_ = std::clamp(percent, 0, 100);
}

void UpdatePopup::show_failed() { state_ = State::Failed; }

void UpdatePopup::paint(Painter& p) {
  if (state_ == State::Hidden) return;
  const Theme& t = ui_.theme;
  p.set_base(D2D1::Matrix3x2F::Identity());
  // .update-popup: padding 10 12 10 16, gap 12; .update-button: padding 6 14, radius 9.
  constexpr float pad_left = 16, pad_right = 12, pad_y = 10, gap = 12, button_pad_x = 14, button_h = 32, margin = 14;
  const TextStyle body{.size = 15, .weight = 700, .line_height = 22, .ellipsis = true};
  const TextStyle label_style{.size = 15, .weight = 700, .line_height = button_h, .align = Align::Center};

  std::string text;
  std::string label;
  switch (state_) {
    case State::Busy:
      text = percent_ >= 100 ? "설치하고 다시 시작하는 중..." : "업데이트 받는 중... " + std::to_string(percent_) + "%";
      break;
    case State::Failed:
      text = "업데이트에 실패했어요.";
      label = "다시 시도";
      break;
    default:
      text = "새로운 버전을 사용할 수 있어요.";
      label = "업데이트";
      break;
  }

  float label_w = 0;
  if (!label.empty()) {
    DWRITE_TEXT_METRICS m{};
    if (const auto layout = ui_.gfx.layout(widen(label), label_style, ui_.width)) layout->GetMetrics(&m);
    label_w = m.widthIncludingTrailingWhitespace + 2 * button_pad_x;
  }
  const float max_w = std::max(120.0f, ui_.width - 100);  // max-width: calc(100vw - 100px)
  const float chrome = pad_left + pad_right + (label_w > 0 ? label_w + gap : 0);
  DWRITE_TEXT_METRICS size{};
  if (const auto layout = ui_.gfx.layout(widen(text), body, std::max(40.0f, max_w - chrome))) layout->GetMetrics(&size);
  const float text_w = std::min(size.widthIncludingTrailingWhitespace + 1, std::max(40.0f, max_w - chrome));
  const float inner_h = label_w > 0 ? button_h : 22;
  const Rect card{margin, ui_.height - margin - inner_h - 2 * pad_y, text_w + chrome, inner_h + 2 * pad_y};

  if (t.dark) p.shadow_round(card, 14, 38, 12, rgb(0x000000, 0.33f));
  else p.shadow_round(card, 14, 16, 4, rgb(0x2e78aa, 0.22f));
  p.fill_round(card, 14, t.surface);
  ui_.add_hit_window(this, card, Card);
  p.text(text, {card.x + pad_left, card.y + (card.h - 22) / 2, text_w, 22}, body, t.hl_text);

  if (label_w > 0) {
    const Rect button{card.right() - pad_right - label_w, card.y + pad_y, label_w, button_h};
    const bool hot = ui_.hot(this, Button);
    if (hot) p.push(scale_about(button.x + button.w / 2, button.y + button.h / 2, 1.03f));
    p.fill_round(button, 9, hot ? t.hl_dark : t.hl);
    p.text(label, button, label_style, t.on_accent);
    if (hot) p.pop();
    ui_.add_hit_window(this, button, Button);
  }
}

void UpdatePopup::click(int id, float, float, bool) {
  if (id == Button && on_update) on_update();
}

Cursor UpdatePopup::cursor(int id) const { return id == Button ? Cursor::Hand : Cursor::Arrow; }

bool UpdatePopup::tick(unsigned long long now_ms) {
  if (check_at_ == 0 || now_ms < check_at_) return false;
  check_at_ = 0;
  if (on_check_due) on_check_due();
  return false;
}

}  // namespace crpp::ui
