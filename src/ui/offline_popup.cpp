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
  const skin::Screen* screen = ui_.pack_screen("offline");
  if (!screen) return {0, 0};
  Shown shown;
  measure_screen(ui_, *screen, ui_.content, shown);
  return shown.size;
}

void OfflinePopup::paint(Painter& p) {
  const skin::Screen* screen = ui_.pack_screen("offline");
  if (!shown_ || covered_ || !screen) return;
  p.set_base(D2D1::Matrix3x2F::Identity());
  Shown shown(settled_);
  draw_layer(ui_, p, this, *screen, ui_.content, shown);
  settled_ = shown.settled;
  // The settings droplet is drawn next, on the stage.
  p.set_base(D2D1::Matrix3x2F::Scale(ui_.scale, ui_.scale) * D2D1::Matrix3x2F::Translation(ui_.ox, ui_.oy));
}

bool OfflinePopup::wheel(float, float, float) { return shown_ && !covered_; }

Cursor OfflinePopup::cursor(int) const { return Cursor::Arrow; }

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
