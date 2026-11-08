#pragma once
// The account prompt (.setup-screen): one input on a card, with whatever a step adds
// around it. It knows nothing of the steps; each one is described by a Prompt.
#include <functional>
#include <string>
#include <string_view>

#include "crpp/skin/pack.hpp"
#include "crpp/ui/surface.hpp"

namespace crpp::ui {

struct Prompt {
  std::string heading, label, placeholder, hint, error, value;
  std::string prefix;         // drawn before the input ("@")
  std::string extra, extra2;  // the buttons on the right; extra2 only beside extra
  std::string link;           // the link below
  std::size_t max_length = 0;
  bool password = false;
  bool lowercase = false;
  bool identifier = false;  // only a-z, 0-9, '_' and '.' can be typed
  bool cancelable = false;  // "← 돌아가기", and Escape does the same
  // Whether the value can be submitted; the arrow lights up when it can.
  std::function<bool(std::string_view)> valid;
  // Replaces `hint` for this value when not empty.
  std::function<std::string(std::string_view)> hint_for;
  // The hint while the field is locked, for this many seconds left.
  std::function<std::string(long long)> lock_hint;
};

class PromptScreen final : public Surface {
 public:
  using Surface::Surface;
  enum Id : int { Field, Submit, Eye, Back, Extra, Extra2, Link };

  std::function<void(std::string value)> on_submit;
  std::function<void()> on_back, on_extra, on_extra2, on_link;

  // What a Skin Pack can name on this screen.
  [[nodiscard]] static skin::ScreenSchema schema();

  void show(Prompt prompt);
  void set_value(std::string_view value);
  void set_error(std::string error);
  // A request is out: nothing can be pressed until it is answered.
  void set_busy(bool busy) { busy_ = busy; }
  // The field empties and takes nothing until the time passes.
  void lock(long long seconds);
  [[nodiscard]] bool locked() const { return lock_until_ != 0; }
  [[nodiscard]] bool busy() const { return busy_; }
  [[nodiscard]] bool password_shown() const { return !field_.password; }
  [[nodiscard]] const Prompt& prompt() const { return prompt_; }
  [[nodiscard]] std::string value() const;
  // The hint as drawn last: an error, the lock's countdown or the step's own.
  [[nodiscard]] const std::string& hint_shown() const { return hint_shown_; }

  void paint(Painter& p);
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] bool acts_on_press(int id) const override { return id == Field; }
  void drag(float x, float y) override;
  void enter(bool shift) override;
  bool escape() override;
  [[nodiscard]] Cursor cursor(int id) const override;
  [[nodiscard]] bool is_link(int id) const override { return id == Link; }
  bool tick(unsigned long long now_ms) override;
  [[nodiscard]] unsigned long long wake_at() const override { return next_wake_; }

 private:
  void paint_pack(Painter& p, const skin::Screen& screen, bool is_locked, bool hint_error);
  [[nodiscard]] TextStyle field_style() const;
 public:
  [[nodiscard]] std::vector<MenuItem> context_menu(int id, float x, float y) override;
  void menu_command(MenuCommand command) override;
 private:
  void submit();
  void take_focus();
  void sync_with_field();
  void plan_wake(unsigned long long now_ms);

  Prompt prompt_;
  TextField field_;
  std::wstring seen_;
  std::string hint_shown_;
  bool valid_ = false;
  bool busy_ = false;
  unsigned long long lock_until_ = 0;
  unsigned long long next_wake_ = 0;
  float scroll_x_ = 0;  // how far the text is scrolled to keep the caret in view
  float text_x_ = 0;    // where the field's text starts, in window coordinates
  TextStyle pack_field_style_;
};

}  // namespace crpp::ui
