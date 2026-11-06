#pragma once
// The main menu: create a Room, join one by code, or find a public one.
#include <functional>
#include <string>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

class HomeScreen final : public Surface {
 public:
  explicit HomeScreen(Ui& ui);

  std::function<void()> on_create;
  std::function<void(std::string code)> on_join;
  std::function<void()> on_find_public;

  void set_nickname(std::string nickname) { nickname_ = std::move(nickname); }
  void set_hour(int hour) { hour_ = hour; }  // 0-23, for the greeting

  void paint(Painter& p);
  [[nodiscard]] bool accepts(int id, float x, float y) const override;
  void click(int id, float x, float y, bool shift) override;
  void enter(bool shift) override;
  [[nodiscard]] Cursor cursor(int id) const override;

 private:
  enum Id : int { Create, Join, Code, Find };
  void join();

  std::string nickname_;
  int hour_ = 12;
  TextField code_;
};

}  // namespace crpp::ui
