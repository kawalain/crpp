#pragma once
// Short messages that go away by themselves: the toast at the top of the window, or
// a bubble above the button the message is about. One of each at a time; showing
// either replaces what was there.
#include <functional>
#include <string>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

class Toasts final : public Surface {
 public:
  using Surface::Surface;
  enum Id : int { Action };

  static constexpr unsigned long long default_ms = 5000;

  [[nodiscard]] static skin::ScreenSchema schema();
  [[nodiscard]] static skin::ScreenSchema bubble_schema();

  // A duration of 0 stays until replaced or cleared.
  void toast(std::string text, unsigned long long duration_ms = default_ms);
  void toast(std::string text, std::string action, std::function<void()> on_action,
             unsigned long long duration_ms = default_ms);
  // anchor is the button, in window coordinates; the bubble's right edge meets the
  // button's, `gap` above it.
  void bubble(Rect anchor, std::string text, float gap = 8, unsigned long long duration_ms = default_ms);
  void clear();

  // The window it was last asked from; the message shows there while that is open.
  [[nodiscard]] int shown_in() const { return shown_in_; }
  void paint(Painter& p);
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] bool animating() const override;
  bool tick(unsigned long long now_ms) override;
  [[nodiscard]] unsigned long long wake_at() const override;

 private:
  int shown_in_ = 0;
  // Showing, or sliding away after hide_at.
  struct Message {
    std::string text;
    unsigned long long shown_at = 0, hide_at = 0;  // hide_at 0 = stays
    [[nodiscard]] bool active() const { return !text.empty(); }
  };
  void paint_toast(Painter& p);
  void paint_bubble(Painter& p);
  [[nodiscard]] float visibility(const Message& m, float duration_ms) const;
  [[nodiscard]] bool up(const Message& m) const { return !ui_.animations || !m.hide_at || ui_.now_ms < m.hide_at; }
  [[nodiscard]] bool moving(const Message& m, float duration_ms, unsigned long long now) const;

  Message toast_;
  std::string action_;
  std::function<void()> on_action_;
  Message bubble_;
  Rect anchor_;
  float gap_ = 8;
  unsigned bubbles_ = 0;
  // How long each takes to come and go (.toast and .btn-notice transition), until a pack reports its own.
  float toast_ms_ = 180, bubble_ms_ = 150;
};

}  // namespace crpp::ui
