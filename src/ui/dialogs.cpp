#include "crpp/ui/dialogs.hpp"

#include <windows.h>

#include <algorithm>

#include "crpp/ui/controls.hpp"

namespace crpp::ui {

void Dialogs::show(Dialog dialog) {
  dialog_ = std::move(dialog);
  opened_at_ = clock_ms();
  was_animating_ = true;
  ui_.set_focus(nullptr, nullptr);
}

void Dialogs::paint(Painter& p) {
  if (!dialog_) return;
  const DialogMetrics m = ui_.skin().dialog_metrics();
  const auto [pad_top, pad_x, pad_bottom, gap, button_h, button_w, button_gap, width, wide_width, margin, text_size, text_line, align] = m;
  const float w = std::min(dialog_->wide ? wide_width : width, ui_.width - margin);
  const TextStyle body{.size = text_size, .weight = 700, .line_height = text_line, .align = align, .wrap = true, .keep_words = true};
  const float text_w = w - 2 * pad_x;
  DWRITE_TEXT_METRICS size{};
  if (const auto layout = ui_.gfx.layout(widen(dialog_->text), body, text_w)) layout->GetMetrics(&size);
  const float h = pad_top + size.height + gap + button_h + pad_bottom;

  const float shown = transition(ui_, opened_at_, modal_pop_ms);
  const Rect card = paint_modal(ui_, this, p, w, h, shown, Backdrop, Card);
  p.text(dialog_->text, {card.x + pad_x, card.y + pad_top, text_w, size.height}, body, with_alpha(ui_.theme.hl_text, shown));

  const bool two = !dialog_->cancel.empty();
  const float row = two ? 2 * button_w + button_gap : button_w;
  const float x = card.x + (w - row) / 2;
  const float y = card.bottom() - pad_bottom - button_h;
  paint_modal_button(ui_, this, p, {x, y, button_w, button_h}, dialog_->confirm, true, shown, Confirm);
  if (two) paint_modal_button(ui_, this, p, {x + button_w + button_gap, y, button_w, button_h}, dialog_->cancel, false, shown, Cancel);
}

void Dialogs::click(int id, float, float, bool) {
  if (!dialog_ || (id != Confirm && id != Cancel)) return;
  // Moved out first: the callback may open the next dialog.
  const Dialog closing = std::move(*dialog_);
  dialog_.reset();
  const auto& action = id == Confirm ? closing.on_confirm : closing.on_cancel;
  if (action) action();
}

bool Dialogs::wheel(float, float, float) { return is_open(); }

bool Dialogs::escape() {
  if (!dialog_ || dialog_->cancel.empty()) return false;
  click(Cancel, 0, 0, false);
  return true;
}

Cursor Dialogs::cursor(int id) const { return id == Confirm || id == Cancel ? Cursor::Hand : Cursor::Arrow; }

bool Dialogs::animating() const { return ui_.animations && was_animating_; }

bool Dialogs::tick(unsigned long long now_ms) {
  if (!was_animating_) return false;
  if (!dialog_ || static_cast<float>(now_ms - opened_at_) >= modal_pop_ms) was_animating_ = false;
  return true;
}

}  // namespace crpp::ui
