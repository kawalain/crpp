#include "crpp/ui/dialogs.hpp"

#include "crpp/ui/controls.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum TextId : int { Text, ConfirmText, CancelText };
enum NumberId : int { Wide, HasCancel, Revision };
enum ReportId : int { Width, Height };
constexpr skin::Name text_names[] = {{"text", Text}, {"confirm", ConfirmText}, {"cancel", CancelText}};
constexpr skin::Name number_names[] = {{"wide", Wide}, {"has_cancel", HasCancel}, {"revision", Revision}};
constexpr skin::Name hit_names[] = {{"backdrop", Dialogs::Backdrop}, {"card", Dialogs::Card}, {"confirm", Dialogs::Confirm}, {"cancel", Dialogs::Cancel}};
constexpr skin::Name report_names[] = {{"width", Width}, {"height", Height}};

class Shown final : public skin::Source {
 public:
  Shown(const Dialog& dialog, unsigned revision) : d_(dialog), revision_(revision) {}

  std::string_view text(int id, int) override {
    switch (id) {
      case Text: return d_.text;
      case ConfirmText: return d_.confirm;
      default: return d_.cancel;
    }
  }
  float number(int id, int) override {
    if (id == Revision) return static_cast<float>(revision_);
    return (id == Wide ? d_.wide : !d_.cancel.empty()) ? 1.0f : 0.0f;
  }
  void report(int id, float value) override { (id == Width ? size.width : size.height) = value; }

  D2D1_SIZE_F size{};

 private:
  const Dialog& d_;
  unsigned revision_;
};

}  // namespace

skin::ScreenSchema Dialogs::schema() { return {.screen = "dialog", .texts = text_names, .numbers = number_names, .hits = hit_names, .reports = report_names}; }

void Dialogs::show(Dialog dialog) {
  // A question that follows another at once keeps its layer: the keyboard starts on its own default.
  if (dialog_ && ui_.stop_owner == this) ui_.set_stop(this, dialog.cannot_undo && !dialog.cancel.empty() ? Cancel : Confirm);
  dialog_ = std::move(dialog);
  ++revision_;
  opened_at_ = clock_ms();
  was_animating_ = true;
  ui_.set_focus(nullptr, nullptr);
}

D2D1_SIZE_F Dialogs::content_size() const {
  const skin::Screen* screen = ui_.pack_screen("dialog");
  if (!dialog_ || !screen) return {0, 0};
  Shown shown(*dialog_, revision_);
  measure_screen(ui_, *screen, ui_.content, shown);
  return shown.size;
}

void Dialogs::paint(Painter& p) {
  const skin::Screen* screen = ui_.pack_screen("dialog");
  if (!dialog_ || !screen) return;
  p.set_base(D2D1::Matrix3x2F::Identity());
  Shown shown(*dialog_, revision_);
  draw_layer(ui_, p, this, *screen, ui_.content, shown);
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

bool Dialogs::tick(unsigned long long now_ms) {
  if (!was_animating_) return false;
  if (!dialog_ || static_cast<float>(now_ms - opened_at_) >= modal_pop_ms) was_animating_ = false;
  return true;
}

}  // namespace crpp::ui
