#pragma once
// The Mini window: a small window, always on top, with every member's disc and mute, for a Room kept beside other
// work. A disc is lit while its member is making a sound and dark after, with no fade, so that the window needs a
// frame only when one starts or ends, also while the main window is hidden.
#include <functional>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

class RoomScreen;

class MiniWindow final : public Surface {
 public:
  MiniWindow(Ui& ui, RoomScreen& room) : Surface(ui), room_(room) {}
  enum Id : int { Close, Mute };
  [[nodiscard]] std::optional<Role> role(int id) const override {
    if (id == Close || id == Mute) return Role::Button;
    return std::nullopt;
  }
  [[nodiscard]] static skin::ScreenSchema schema();

  void open() { open_ = true; }
  void close() { open_ = false; }
  void toggle() { open_ = !open_; }
  [[nodiscard]] bool is_open() const { return open_; }
  void set_active(bool active) { active_ = active; }

  // Its window's size and how round its corners are, as the pack reports them.
  struct Shape {
    float width = 0, height = 0, radius = 0;
  };
  [[nodiscard]] Shape shape() const;
  // Fills the window, whose whole area this draws.
  void paint(Painter& p);
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] Cursor cursor(int id) const override;

 private:
  RoomScreen& room_;
  bool open_ = false, active_ = true;
};

}  // namespace crpp::ui
