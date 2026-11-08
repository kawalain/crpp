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
  p.set_base(D2D1::Matrix3x2F::Identity());

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
