#include "crpp/ui/notice_card.hpp"

#include <algorithm>

namespace crpp::ui {

void NoticeCard::paint(Painter& p) {
  if (!notice_) return;
  const Theme& t = ui_.theme;
  p.set_base(D2D1::Matrix3x2F::Identity());
  const bool plain = !ui_.skin().moves();
  const float pad = plain ? 10 : 18, gap = plain ? 10 : 14, button_h = plain ? 23 : 34, button_pad = plain ? 12 : 14;
  const float w = std::min(380.0f, ui_.width - 32);  // leaves the chat input free
  const TextStyle body{.size = 16, .line_height = 23, .wrap = true};
  DWRITE_TEXT_METRICS text_size{};
  if (const auto layout = ui_.gfx.layout(widen(notice_->text), body, w - 2 * pad)) layout->GetMetrics(&text_size);
  const float h = pad + text_size.height + gap + button_h + pad;
  const Rect card{plain ? ui_.content.x + 8 : 16, plain ? ui_.content.bottom() - 8 - h : ui_.height - 72 - h, w, h};  // above the leave button
  ui_.skin().panel(p, t, card, PanelKind::Notice);
  ui_.add_hit_window(this, card, Card);
  p.text(notice_->text, {card.x + pad, card.y + pad, w - 2 * pad, text_size.height}, body, t.ink_strong);

  const TextStyle label{.size = 15, .weight = 700, .line_height = button_h, .align = Align::Center};
  float right = card.right() - pad;
  const auto button = [&](const std::string& text, int id, bool primary) {
    DWRITE_TEXT_METRICS size{};
    if (const auto layout = ui_.gfx.layout(widen(text), label, w)) layout->GetMetrics(&size);
    const float bw = size.widthIncludingTrailingWhitespace + 2 * button_pad;
    const Rect r{right - bw, card.bottom() - pad - button_h, bw, button_h};
    ui_.skin().button(p, t, r, text, {.kind = ButtonKind::Notice, .primary = primary, .hot = ui_.hot(this, id)});
    ui_.add_hit_window(this, r, id);
    right = r.x - 6;
  };
  if (!notice_->action.empty()) button(notice_->action, Action, true);
  button(notice_->dismiss, Dismiss, notice_->action.empty());
}

void NoticeCard::click(int id, float, float, bool) {
  if (id == Action) {
    // Moved out first: the action may set the next notice and its action.
    const auto action = std::move(on_action);
    on_action = nullptr;
    notice_.reset();
    if (action) action();
  } else if (id == Dismiss) {
    notice_.reset();
  }
}

Cursor NoticeCard::cursor(int id) const { return id == Card ? Cursor::Arrow : Cursor::Hand; }

}  // namespace crpp::ui
