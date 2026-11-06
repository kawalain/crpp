#pragma once
// The main menu: create a Room, join one by code, or find a public one.
#include <functional>
#include <string>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

class HomeScreen final : public Surface {
 public:
  explicit HomeScreen(Ui& ui);

  enum Id : int { Create, Join, Code, Find };

  std::function<void()> on_create;
  std::function<void(std::string code)> on_join;
  std::function<void()> on_find_public;
  // A Room is being entered: the buttons do nothing and nothing about them changes.
  std::function<bool()> busy;

  void set_nickname(std::string nickname) { nickname_ = std::move(nickname); }
  void set_hour(int hour) { hour_ = hour; }  // 0-23, for the greeting
  // A Room could not be entered: says so above the button that tried, or at the top
  // of the window when no button did.
  void show_error(std::string text);

  void paint(Painter& p);
  [[nodiscard]] bool accepts(int id, float x, float y) const override;
  void click(int id, float x, float y, bool shift) override;
  void enter(bool shift) override;
  [[nodiscard]] Cursor cursor(int id) const override;

 private:
  [[nodiscard]] bool is_busy() const { return busy && busy(); }
  void join();

  std::string nickname_;
  int hour_ = 12;
  TextField code_;
  Id last_action_ = Create;
};

}  // namespace crpp::ui
