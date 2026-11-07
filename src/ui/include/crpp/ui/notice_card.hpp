#pragma once
// A message in the window's corner that never blocks the screen under it.
#include <functional>
#include <optional>
#include <string>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

struct Notice {
  std::string text;
  std::string action;   // empty = no action button
  std::string dismiss;  // the label of the closing button
};

class NoticeCard final : public Surface {
 public:
  using Surface::Surface;
  enum Id : int { Card, Action, Dismiss };

  [[nodiscard]] static skin::ScreenSchema schema();

  // Runs once, when the action button is pressed; the notice closes.
  std::function<void()> on_action;

  void set(std::optional<Notice> notice) { notice_ = std::move(notice); }

  void paint(Painter& p);
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] Cursor cursor(int id) const override;

 private:
  std::optional<Notice> notice_;
};

}  // namespace crpp::ui
