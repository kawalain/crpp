#pragma once
// The window's frame, caption and menu bar, for a Skin that draws its own. The window asks
// where a point falls and leaves dragging, resizing and Snap to the system; minimize, close
// and the menus are ordinary surfaces.
#include <functional>
#include <string>

#include "crpp/skin/pack.hpp"
#include "crpp/ui/surface.hpp"

namespace crpp::ui {

// The sizes of a window frame the app draws itself.
struct FrameMetrics {
  float caption_h = 28, frame_w = 3, menu_h = 20, radius = 8;
  float edge = 4, corner = 12;  // how near an edge or a corner resizes
  float button = 21, button_gap = 2, button_top = 4, button_right = 5;
};

class WindowFrame final : public Surface {
 public:
  using Surface::Surface;
  enum Id : int { Minimize, Close, MenuBase = 10, ItemBase = 100 };

  std::function<void()> on_minimize, on_close, on_exit, on_leave_room, on_options, on_changelog;

  // What a Skin Pack can name here: the frame, and the menu that is down.
  [[nodiscard]] static skin::ScreenSchema schema();
  [[nodiscard]] static skin::ScreenSchema menu_schema();

  // What the system should do with a point.
  enum class Zone : std::uint8_t { Client, Caption, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };
  struct Parts {
    bool resizable = true, minimize = true, close = true;
  };
  [[nodiscard]] static Zone zone_at(float x, float y, float width, float height, Parts parts = {}, const FrameMetrics& m = {});
  // Inside the frame and under the caption, and the menu bar when there is one.
  [[nodiscard]] static Rect content(float width, float height, bool with_menu, const FrameMetrics& m = {});
  // All but the caption, which stays usable under a modal.
  [[nodiscard]] static Rect below_caption(float width, float height, const FrameMetrics& m = {});

  void set_title(std::string title) { title_ = std::move(title); }
  void set_active(bool active) { active_ = active; }
  void set_menu_shown(bool shown) { menu_shown_ = shown; }
  void set_parts(Parts parts) { parts_ = parts; }
  void set_in_room(bool in_room) { in_room_ = in_room; }
  [[nodiscard]] bool menu_open() const { return open_ >= 0; }

  void paint(Painter& p);
  // The open menu, over everything else.
  void paint_menu(Painter& p);
  void before_click(const Surface* owner, int id) override;
  void click(int id, float x, float y, bool shift) override;
  [[nodiscard]] bool acts_on_press(int id) const override { return id >= MenuBase && id < ItemBase; }
  bool escape() override;
  [[nodiscard]] Cursor cursor(int /*id*/) const override { return Cursor::Arrow; }

 private:
  struct Item {
    const char* text;
    std::function<void()>* action;
  };
  [[nodiscard]] std::vector<Item> items_of(int menu);
  void paint_pack(Painter& p, const skin::Screen& screen);
  void paint_menu_pack(Painter& p, const skin::Screen& screen);

  std::string title_ = "Cloudroom++";
  bool active_ = true, menu_shown_ = true, in_room_ = false;
  Parts parts_;
  static constexpr int menus = 3;
  int open_ = -1;  // which menu is down, below menus
  Rect titles_[menus];
};

}  // namespace crpp::ui
