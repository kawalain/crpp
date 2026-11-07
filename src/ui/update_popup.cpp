#include "crpp/ui/update_popup.hpp"

#include <algorithm>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum TextId : int { Text, Label };
constexpr skin::Name text_names[] = {{"text", Text}, {"label", Label}};
constexpr skin::Name number_names[] = {{"has_label", 0}};
constexpr skin::Name hit_names[] = {{"card", UpdatePopup::Card}, {"button", UpdatePopup::Button}};

}  // namespace

skin::ScreenSchema UpdatePopup::schema() { return {.screen = "update_popup", .texts = text_names, .numbers = number_names, .hits = hit_names}; }

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
  const bool plain = !ui_.skin().moves();
  const float pad_left = plain ? 10 : 16, pad_right = plain ? 6 : 12, pad_y = plain ? 5 : 10, gap = plain ? 8 : 12, button_pad_x = plain ? 12 : 14,
              button_h = plain ? 23 : 32, margin = 14;
  const TextStyle body{.size = 15, .weight = 700, .line_height = 22, .ellipsis = true};
  const TextStyle label_style{.size = 15, .weight = 700, .line_height = button_h, .align = Align::Center};

  std::string text;
  std::string label;
  switch (state_) {
    case State::Busy:
      text = percent_ >= 100 ? std::string(tr().update_installing) : locale::fill(tr().update_downloading, {std::to_string(percent_)});
      break;
    case State::Failed:
      text = tr().update_failed_short;
      label = tr().update_retry;
      break;
    default:
      text = tr().update_available;
      label = tr().update_button;
      break;
  }

  if (const skin::Screen* screen = ui_.pack_screen("update_popup")) {
    class Shown final : public skin::Source {
     public:
      Shown(std::string_view words, std::string_view button) : text_(words), label_(button) {}
      std::string_view text(int id, int) override { return id == Text ? text_ : label_; }
      float number(int, int) override { return label_.empty() ? 0.0f : 1.0f; }

     private:
      std::string_view text_, label_;
    } shown(text, label);
    draw_layer(ui_, p, this, *screen, ui_.content, shown);
    return;
  }

  float label_w = 0;
  if (!label.empty()) {
    DWRITE_TEXT_METRICS m{};
    if (const auto layout = ui_.gfx.layout(widen(label), label_style, ui_.width)) layout->GetMetrics(&m);
    label_w = m.widthIncludingTrailingWhitespace + 2 * button_pad_x;
  }
  const float max_w = plain ? ui_.content.w - 16 : std::max(120.0f, ui_.width - 100);  // max-width: calc(100vw - 100px)
  const float chrome = pad_left + pad_right + (label_w > 0 ? label_w + gap : 0);
  DWRITE_TEXT_METRICS size{};
  if (const auto layout = ui_.gfx.layout(widen(text), body, std::max(40.0f, max_w - chrome))) layout->GetMetrics(&size);
  const float text_w = std::min(size.widthIncludingTrailingWhitespace + 1, std::max(40.0f, max_w - chrome));
  const float inner_h = label_w > 0 ? button_h : 22;
  const Rect card{plain ? ui_.content.x + 8 : margin, plain ? ui_.content.y + 8 : margin, text_w + chrome, inner_h + 2 * pad_y};

  ui_.skin().panel(p, t, card, PanelKind::Update);
  ui_.add_hit_window(this, card, Card);
  p.text(text, {card.x + pad_left, card.y + (card.h - 22) / 2, text_w, 22}, body, t.hl_text);

  if (label_w > 0) {
    const Rect button{card.right() - pad_right - label_w, card.y + pad_y, label_w, button_h};
    ui_.skin().button(p, t, button, label, {.kind = ButtonKind::Update, .hot = ui_.hot(this, Button)});
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
