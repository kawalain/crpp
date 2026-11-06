#include "crpp/ui/settings_tabs.hpp"

namespace crpp::ui {

namespace {

constexpr float sounds_top = row_top + row_step * 2 + 40;

}  // namespace

void NotifyTab::set_sounds(std::vector<NotifySound> sounds) {
  sounds_ = std::move(sounds);
  rows_ = std::vector<Row>(sounds_.size());
}

void NotifyTab::shown() {
  if (on_shown) on_shown();
}

float NotifyTab::content_height() const {
  const float rows = static_cast<float>(sounds_.size());
  const float bottom = rows > 0 ? sounds_top + row_top - 25 + (rows - 1) * row_step + 36 : sounds_top + 32;
  return bottom + settings_bottom_pad;
}

void NotifyTab::paint(Painter& p, Rect area) {
  const Theme& t = ui_.theme;
  const Prefs& prefs = *ui_.prefs;
  paint_heading(p, t, area, 25, "알림");
  paint_subheading(p, t, area, row_top - 6, "메시지 수신 시");
  paint_toggle(ui_, this, p, area, row_top + 34, "트레이 알림", prefs.get(pref::chat_tray_notify), TrayToggle, tray_);
  paint_toggle(ui_, this, p, area, row_top + 34 + row_step, "작업표시줄 아이콘 점멸", prefs.get(pref::chat_taskbar_flash),
               FlashToggle, flash_);
  paint_heading(p, t, area, sounds_top, "알림음");

  const TextStyle name_style{.size = 18, .line_height = 36};
  for (std::size_t i = 0; i < sounds_.size(); ++i) {
    const float top = sounds_top + row_top - 25 + static_cast<float>(i) * row_step;
    const int preview = PreviewBase + static_cast<int>(i);
    Row& row = rows_[i];
    paint_toggle(ui_, this, p, area, top, {}, prefs.notification_sound(sounds_[i].id), SoundBase + static_cast<int>(i), row.toggle);

    // .notice-name: grows from its left edge under the pointer, with its tip above.
    const float w = text_width(p, sounds_[i].name, name_style);
    const Rect name{area.x + 16, area.y + top, w, 36};
    const bool hot = ui_.hot(this, preview);
    row.grow.set(ui_, hot, 140);
    row.tip.set(ui_, hot, 150);
    const float g = row.grow.value(ui_);
    if (g > 0) p.push(scale_about(name.x, name.y + 18, 1 + 0.07f * g));
    p.text(sounds_[i].name, {name.x, name.y, 400, 36}, name_style, t.hl_text);
    if (const float shown = row.tip.value(ui_); shown > 0) {
      constexpr const char* tip = "미리 듣기";
      const TextStyle style{.size = 14, .weight = 700, .line_height = 20};
      const float tip_w = text_width(p, tip, style) + 24;
      paint_bubble(p, tip, {name.x + w / 2 - tip_w / 2, name.y - 2 - 26 + 4 * (1 - shown), tip_w, 26}, style, 10, t.hl,
                   t.on_accent, shown);
    }
    if (g > 0) p.pop();
    ui_.add_hit(this, name, preview);
  }
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

bool NotifyTab::animating() const {
  if (tray_.running(ui_) || flash_.running(ui_)) return true;
  for (const Row& row : rows_) {
    if (row.toggle.running(ui_) || row.grow.running(ui_) || row.tip.running(ui_)) return true;
  }
  return false;
}

}  // namespace crpp::ui
