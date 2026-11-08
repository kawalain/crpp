#include "crpp/ui/toasts.hpp"

#include <windows.h>

#include <algorithm>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {
enum TextId : int { Text, ActionText };
enum NumberId : int { HasAction, Up };
enum BubbleNumberId : int { AnchorRight, AnchorY, Gap, BubbleUp, Again };
constexpr skin::Name text_names[] = {{"text", Text}, {"action", ActionText}};
constexpr skin::Name number_names[] = {{"has_action", HasAction}, {"up", Up}};
constexpr skin::Name report_names[] = {{"ms", 0}};
constexpr skin::Name hit_names[] = {{"action", Toasts::Action}};
constexpr skin::Name bubble_text_names[] = {{"text", Text}};
constexpr skin::Name bubble_number_names[] = {{"anchor_right", AnchorRight}, {"anchor_y", AnchorY}, {"gap", Gap}, {"up", BubbleUp}, {"again", Again}};
}  // namespace

skin::ScreenSchema Toasts::schema() { return {.screen = "toast", .texts = text_names, .numbers = number_names, .hits = hit_names, .reports = report_names}; }
skin::ScreenSchema Toasts::bubble_schema() { return {.screen = "bubble", .texts = bubble_text_names, .numbers = bubble_number_names, .reports = report_names}; }

void Toasts::toast(std::string text, unsigned long long duration_ms) { toast(std::move(text), {}, nullptr, duration_ms); }

void Toasts::toast(std::string text, std::string action, std::function<void()> on_action, unsigned long long duration_ms) {
  shown_in_ = ui_.window;
  const auto now = clock_ms();
  bubble_ = {};
  // A toast already up changes its words without sliding in again.
  const bool up = toast_.active() && (toast_.hide_at == 0 || now < toast_.hide_at);
  toast_ = {std::move(text), up ? toast_.shown_at : now, duration_ms ? now + duration_ms : 0};
  action_ = std::move(action);
  on_action_ = std::move(on_action);
}

void Toasts::bubble(Rect anchor, std::string text, float gap, unsigned long long duration_ms) {
  shown_in_ = ui_.window;
  const auto now = clock_ms();
  toast_ = {};
  on_action_ = nullptr;
  bubble_ = {std::move(text), now, duration_ms ? now + duration_ms : 0};
  anchor_ = anchor;
  gap_ = gap;
  ++bubbles_;
}

void Toasts::paint(Painter& p) {
  p.set_base(D2D1::Matrix3x2F::Identity());
  if (toast_.active()) paint_toast(p);
  if (bubble_.active()) paint_bubble(p);
}

void Toasts::paint_toast(Painter& p) {
  if (const skin::Screen* screen = ui_.pack_screen("toast")) {
    class Shown final : public skin::Source {
     public:
      Shown(std::string_view words, std::string_view action, bool up, float& ms) : text_(words), action_(action), up_(up), ms_(ms) {}
      std::string_view text(int id, int) override { return id == Text ? text_ : action_; }
      float number(int id, int) override { return (id == Up ? up_ : !action_.empty()) ? 1.0f : 0.0f; }
      void report(int, float value) override { ms_ = value; }

     private:
      std::string_view text_, action_;
      bool up_;
      float& ms_;
    } shown(toast_.text, action_, up(toast_), toast_ms_);
    draw_layer(ui_, p, this, *screen, ui_.content, shown);
  }
}

void Toasts::paint_bubble(Painter& p) {
  if (const skin::Screen* screen = ui_.pack_screen("bubble")) {
    class Shown final : public skin::Source {
     public:
      Shown(std::string_view words, Rect anchor, float gap, bool up, unsigned again, float& ms) : text_(words), anchor_(anchor), gap_(gap), up_(up), again_(again), ms_(ms) {}
      std::string_view text(int, int) override { return text_; }
      float number(int id, int) override {
        switch (id) {
          case AnchorRight: return anchor_.right();
          case AnchorY: return anchor_.y;
          case BubbleUp: return up_ ? 1.0f : 0.0f;
          case Again: return static_cast<float>(again_);
          default: return gap_;
        }
      }
      void report(int, float value) override { ms_ = value; }

     private:
      std::string_view text_;
      Rect anchor_;
      float gap_;
      bool up_;
      unsigned again_;
      float& ms_;
    } shown(bubble_.text, anchor_, gap_, up(bubble_), bubbles_, bubble_ms_);
    draw_layer(ui_, p, this, *screen, {0, 0, ui_.width, ui_.height}, shown);
  }
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
  return moving(toast_, toast_ms_, now) || moving(bubble_, bubble_ms_, now);
}

bool Toasts::tick(unsigned long long now_ms) {
  const bool was_moving = moving(toast_, toast_ms_, now_ms) || moving(bubble_, bubble_ms_, now_ms);
  const auto expired = [&](const Message& m, float duration_ms) {
    return m.active() && m.hide_at && static_cast<float>(static_cast<long long>(now_ms - m.hide_at)) >= duration_ms &&
           now_ms >= m.hide_at;
  };
  bool changed = was_moving;
  if (expired(toast_, toast_ms_)) {
    toast_ = {};
    on_action_ = nullptr;
    changed = true;
  }
  if (expired(bubble_, bubble_ms_)) {
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
