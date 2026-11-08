#pragma once
// The modal question: a message with one or two buttons. For a card with other
// content, build a Surface on paint_modal instead.
#include <functional>
#include <optional>
#include <string>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

struct Dialog {
  std::string text;     // '\n' breaks lines
  std::string confirm;  // the primary button
  std::string cancel;   // empty = no cancel button, and Escape does not close it
  bool wide = false;    // 380 wide instead of 320 (.modal-card.wide)
  std::function<void()> on_confirm;
  std::function<void()> on_cancel;
};

class Dialogs final : public Surface {
 public:
  using Surface::Surface;
  enum Id : int { Backdrop, Card, Confirm, Cancel };

  [[nodiscard]] static skin::ScreenSchema schema();

  void show(Dialog dialog);
  void close() { dialog_.reset(); }
  [[nodiscard]] bool is_open() const { return dialog_.has_value(); }
  [[nodiscard]] bool has_cancel() const { return dialog_ && !dialog_->cancel.empty(); }
  // Counts the questions, so a window of its own knows when to fit the next one.
  [[nodiscard]] unsigned revision() const { return revision_; }
  // What the question takes inside a window of its own.
  [[nodiscard]] D2D1_SIZE_F content_size() const;
  // The question on screen; empty when closed.
  [[nodiscard]] std::string_view text() const { return dialog_ ? std::string_view(dialog_->text) : std::string_view(); }

  void paint(Painter& p);
  void click(int id, float x, float y, bool shift) override;
  bool wheel(float x, float y, float delta) override;
  bool escape() override;
  [[nodiscard]] Cursor cursor(int id) const override;
  bool tick(unsigned long long now_ms) override;

 private:
  std::optional<Dialog> dialog_;
  unsigned revision_ = 0;
  unsigned long long opened_at_ = 0;
  bool was_animating_ = false;
};

}  // namespace crpp::ui
