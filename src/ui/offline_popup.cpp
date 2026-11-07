#include "crpp/ui/offline_popup.hpp"

#include <windows.h>

#include <algorithm>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum ReportId : int { Width, Height, Rise };
constexpr skin::Name text_names[] = {{"message", 0}};
constexpr skin::Name number_names[] = {{"settled", 0}};
constexpr skin::Name hit_names[] = {{"backdrop", OfflinePopup::Backdrop}, {"card", OfflinePopup::Card}};
constexpr skin::Name report_names[] = {{"width", Width}, {"height", Height}, {"shown", Rise}};

class Shown final : public skin::Source {
 public:
  explicit Shown(bool settled = true) : settled(settled) {}
  std::string_view text(int, int) override { return tr().offline_message; }
  float number(int, int) override { return settled ? 1.0f : 0.0f; }
  void report(int id, float value) override {
    if (id == Rise) settled = value >= 1;
    else (id == Width ? size.width : size.height) = value;
  }

  D2D1_SIZE_F size{};
  bool settled;
};

}  // namespace

skin::ScreenSchema OfflinePopup::schema() { return {.screen = "offline", .texts = text_names, .numbers = number_names, .hits = hit_names, .reports = report_names}; }

void OfflinePopup::show() {
  if (shown_) return;
  shown_ = true;
  opened_at_ = clock_ms();
  was_animating_ = true;
  settled_ = false;
  if (!covered_) ui_.set_focus(nullptr, nullptr);  // nothing beneath can be used now
}

void OfflinePopup::hide() {
  shown_ = false;
  was_animating_ = false;
  retry_at_ = 0;
}

D2D1_SIZE_F OfflinePopup::content_size() const {
  if (const skin::Screen* screen = ui_.pack_screen("offline")) {
    Shown shown;
    measure_screen(ui_, *screen, ui_.content, shown);
    return shown.size;
  }
  const DialogMetrics m = ui_.skin().dialog_metrics();
  const TextStyle body{.size = m.text_size, .weight = 700, .line_height = m.text_line, .align = m.align, .wrap = true, .keep_words = true};
  DWRITE_TEXT_METRICS size{};
  if (const auto layout = ui_.gfx.layout(widen(tr().offline_message), body, m.width - 2 * m.pad_x)) layout->GetMetrics(&size);
  return {m.width, m.pad_top + size.height + m.pad_bottom};
}

void OfflinePopup::paint(Painter& p) {
  if (!shown_ || covered_) return;
  // The settings droplet is drawn next, on the stage.
  const auto on_stage = D2D1::Matrix3x2F::Scale(ui_.scale, ui_.scale) * D2D1::Matrix3x2F::Translation(ui_.ox, ui_.oy);
  if (const skin::Screen* screen = ui_.pack_screen("offline")) {
    p.set_base(D2D1::Matrix3x2F::Identity());
    Shown shown(settled_);
    draw_layer(ui_, p, this, *screen, ui_.content, shown);
    settled_ = shown.settled;
    p.set_base(on_stage);
    return;
  }
  const auto [pad_top, pad_x, pad_bottom, gap, button_h, button_w, button_gap, width, wide_width, margin, text_size, text_line, align] = ui_.skin().dialog_metrics();
  const float w = ui_.window != 0 ? width : std::min(width, ui_.width - margin);
  const TextStyle body{.size = text_size, .weight = 700, .line_height = text_line, .align = align, .wrap = true, .keep_words = true};
  const float text_w = w - 2 * pad_x;
  DWRITE_TEXT_METRICS size{};
  if (const auto layout = ui_.gfx.layout(widen(tr().offline_message), body, text_w)) layout->GetMetrics(&size);

  const float shown = transition(ui_, opened_at_, modal_pop_ms);
  const Rect card = paint_modal(ui_, this, p, w, pad_top + size.height + pad_bottom, shown, Backdrop, Card);
  p.text(tr().offline_message, {card.x + pad_x, card.y + pad_top, text_w, size.height}, body, with_alpha(ui_.theme.hl_text, shown));
  p.set_base(on_stage);
}

bool OfflinePopup::wheel(float, float, float) { return shown_ && !covered_; }

Cursor OfflinePopup::cursor(int) const { return Cursor::Arrow; }

bool OfflinePopup::animating() const { return ui_.animations && was_animating_ && !ui_.pack_screen("offline"); }

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
