#include "crpp/ui/settings_tabs.hpp"

namespace crpp::ui {

void NotifyTab::set_sounds(std::vector<NotifySound> sounds) {
  sounds_ = std::move(sounds);
}

void NotifyTab::shown() {
  if (on_shown) on_shown();
}

void NotifyTab::paint(Painter& p, Rect area) {
  if (const skin::Screen* screen = ui_.pack_screen("settings_notify")) paint_pack(p, *screen, area);
}

void NotifyTab::click(int id, float, float, bool) {
  Prefs& prefs = *ui_.prefs;
  if (id == TrayToggle) {
    prefs.set(pref::chat_tray_notify, !prefs.get(pref::chat_tray_notify));
  } else if (id == FlashToggle) {
    prefs.set(pref::chat_taskbar_flash, !prefs.get(pref::chat_taskbar_flash));
  } else if (id >= PreviewBase) {
    const auto i = static_cast<std::size_t>(id - PreviewBase);
    if (i < sounds_.size() && on_preview) on_preview(sounds_[i].stem);
  } else if (id >= SoundBase) {
    const auto i = static_cast<std::size_t>(id - SoundBase);
    if (i < sounds_.size()) prefs.set_notification_sound(sounds_[i].id, !prefs.notification_sound(sounds_[i].id));
  }
}

}  // namespace crpp::ui
