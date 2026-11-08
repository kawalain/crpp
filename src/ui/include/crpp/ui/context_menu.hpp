#pragma once
// The menu of what can be done with text, opened by a right-click, the menu key or
// Shift+F10. Upstream has none. It is drawn by the pack's screen `context_menu` in the
// window it was opened in; a pack without that screen shows no menu, and the keys still work.
#include <string>
#include <vector>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

class ContextMenu final : public Surface {
 public:
  using Surface::Surface;
  enum Id : int { Card, Item = 1 };  // Item + index

  [[nodiscard]] static skin::ScreenSchema schema();

  // x, y in window coordinates. From the keyboard the first item that can act is chosen.
  void open(Surface* target, std::vector<MenuItem> items, float x, float y, bool from_keyboard);
  void close();
  [[nodiscard]] bool is_open() const { return target_ != nullptr; }
  // The window it is in: 0 the main one, otherwise a panel's.
  [[nodiscard]] int window() const { return window_; }
  [[nodiscard]] const std::vector<MenuItem>& items() const { return items_; }
  [[nodiscard]] int chosen() const { return chosen_; }

  void paint(Painter& p);
  void before_click(const Surface* owner, int id) override;
  void click(int id, float x, float y, bool shift) override;
  bool escape() override;
  // Every key goes here while it is open, as into any menu.
  bool key(unsigned vk, bool ctrl, bool shift) override;
  [[nodiscard]] Cursor cursor(int) const override { return Cursor::Arrow; }

  // What an item says, without its access key's mark, and that key.
  [[nodiscard]] static std::string label(MenuCommand command);
  [[nodiscard]] static wchar_t access_key(MenuCommand command);
  [[nodiscard]] static const char* shortcut(MenuCommand command);

 private:
  void run(std::size_t index);
  void choose(int from, int step);
  void paint_pack(Painter& p, const skin::Screen& screen);

  Surface* target_ = nullptr;
  std::vector<MenuItem> items_;
  std::vector<std::string> labels_;
  float x_ = 0, y_ = 0;
  int window_ = 0;
  int chosen_ = -1;
  float width_ = 0, height_ = 0;  // of the window when it opened: another size closes it
};

}  // namespace crpp::ui
