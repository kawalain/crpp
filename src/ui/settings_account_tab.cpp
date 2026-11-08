#include "crpp/ui/emoji_picker.hpp"
#include "crpp/ui/settings_tabs.hpp"

namespace crpp::ui {

void AccountTab::paint(Painter& p, Rect area) {
  if (const skin::Screen* screen = ui_.pack_screen("settings_account")) paint_pack(p, *screen, area);
}

void AccountTab::click(int id, float, float, bool) {
  switch (id) {
    case Emoji:
      ui_.emoji_picker->show(profile_.emoji, [this](std::string picked) {
        if (on_emoji) on_emoji(std::move(picked));
      });
      break;
    case Nickname:
      if (on_change_nickname) on_change_nickname();
      break;
    case Handle:
      if (on_change_handle) on_change_handle();
      break;
    case Logout:
      if (on_logout) on_logout();
      break;
    default:
      break;
  }
}

}  // namespace crpp::ui
