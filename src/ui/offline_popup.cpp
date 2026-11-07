#include "crpp/ui/offline_popup.hpp"

#include <windows.h>

#include <algorithm>

#include "crpp/ui/controls.hpp"

namespace crpp::ui {

namespace {
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

D2D1_SIZE_F OfflinePopup::content_size() const {
  const DialogMetrics m = ui_.skin().dialog_metrics();
  const TextStyle body{.size = m.text_size, .weight = 700, .line_height = m.text_line, .align = m.align, .wrap = true, .keep_words = true};
  DWRITE_TEXT_METRICS size{};
  if (const auto layout = ui_.gfx.layout(widen(tr().offline_message), body, m.width - 2 * m.pad_x)) layout->GetMetrics(&size);
  return {m.width, m.pad_top + size.height + m.pad_bottom};
}

void OfflinePopup::paint(Painter& p) {
  if (!shown_ || covered_) return;
  const auto [pad_top, pad_x, pad_bottom, gap, button_h, button_w, button_gap, width, wide_width, margin, text_size, text_line, align] = ui_.skin().dialog_metrics();
  const float w = ui_.window != 0 ? width : std::min(width, ui_.width - margin);
  const TextStyle body{.size = text_size, .weight = 700, .line_height = text_line, .align = align, .wrap = true, .keep_words = true};
  const float text_w = w - 2 * pad_x;
  DWRITE_TEXT_METRICS size{};
  if (const auto layout = ui_.gfx.layout(widen(tr().offline_message), body, text_w)) layout->GetMetrics(&size);

  const float shown = transition(ui_, opened_at_, modal_pop_ms);
  const Rect card = paint_modal(ui_, this, p, w, pad_top + size.height + pad_bottom, shown, Backdrop, Card);
  p.text(tr().offline_message, {card.x + pad_x, card.y + pad_top, text_w, size.height}, body, with_alpha(ui_.theme.hl_text, shown));
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
