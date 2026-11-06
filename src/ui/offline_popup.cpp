#include "crpp/ui/offline_popup.hpp"

#include <windows.h>

#include <algorithm>

#include "crpp/ui/controls.hpp"

namespace crpp::ui {

namespace {
constexpr const char* message = "서버와 연결이 끊겼습니다.\n재연결 시도 중";
}

void OfflinePopup::show() {
  if (shown_) return;
  shown_ = true;
  opened_at_ = clock_ms();
  was_animating_ = true;
  if (!covered_) ui_.set_focus(nullptr, nullptr);  // nothing beneath can be used now
}

void OfflinePopup::hide() {
  shown_ = false;
  was_animating_ = false;
  retry_at_ = 0;
}

void OfflinePopup::paint(Painter& p) {
  if (!shown_ || covered_) return;
  constexpr float pad_top = 24, pad_x = 25, pad_bottom = 20;
  const float w = std::min(320.0f, ui_.width - 40);
  const TextStyle body{.size = 18, .weight = 700, .line_height = 27, .align = Align::Center, .wrap = true};
  const float text_w = w - 2 * pad_x;
  DWRITE_TEXT_METRICS size{};
  if (const auto layout = ui_.gfx.layout(widen(message), body, text_w)) layout->GetMetrics(&size);

  const float shown = transition(ui_, opened_at_, modal_pop_ms);
  const Rect card = paint_modal(ui_, this, p, w, pad_top + size.height + pad_bottom, shown, Backdrop, Card);
  p.text(message, {card.x + pad_x, card.y + pad_top, text_w, size.height}, body, with_alpha(ui_.theme.hl_text, shown));
  // The settings droplet is drawn next, on the stage.
  p.set_base(D2D1::Matrix3x2F::Scale(ui_.scale, ui_.scale) * D2D1::Matrix3x2F::Translation(ui_.ox, ui_.oy));
}

bool OfflinePopup::wheel(float, float, float) { return shown_ && !covered_; }

Cursor OfflinePopup::cursor(int) const { return Cursor::Arrow; }

bool OfflinePopup::animating() const { return ui_.animations && was_animating_; }

bool OfflinePopup::tick(unsigned long long now_ms) {
  bool changed = false;
  if (was_animating_) {
    if (static_cast<float>(now_ms - opened_at_) >= modal_pop_ms) was_animating_ = false;
    changed = true;
  }
  if (retry_at_ != 0 && now_ms >= retry_at_) {
    retry_at_ = 0;
    if (on_retry) on_retry();
  }
  return changed;
}

}  // namespace crpp::ui
