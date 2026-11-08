#include <algorithm>

#include "crpp/ui/dialogs.hpp"
#include "crpp/ui/settings_tabs.hpp"

namespace crpp::ui {

namespace {

constexpr std::array<int, 4> release_seconds{0, 10, 60, -1};  // the last: never

// Differs from upstream: no sleep-on-minimize or input log rows, and the Detail mode
// row replaces "키보드 문자열 구분". Its description is three lines where upstream's is
// one, so what follows moves down by what is left over: advanced_top is where upstream's
// time section would be, 254, with one row out and two lines in. Time and language are on
// the display tab.
constexpr float detail_top = row_top + row_step * 2;
constexpr float advanced_top = 274;
constexpr float release_top = advanced_top + 51;  // not upstream's
constexpr float logging_top = release_top + row_step;
constexpr float open_logs_top = logging_top + row_step;
constexpr float buttons_x = 381, buttons_w = 140, buttons_h = 38;

float reset_top(bool logging) { return (logging ? open_logs_top + buttons_h : logging_top + 36) + 32; }

}  // namespace

SystemTab::SystemTab(Ui& ui) : SettingsTab(ui) { release_.id_base = ReleaseList; }

// A value no choice has, as from a file edited by hand, shows as the longest wait below it.
void SystemTab::sync_release() {
  release_.options = {tr().system_release_now, tr().system_release_10s, tr().system_release_1m, tr().system_release_off};
  const int seconds = ui_.prefs->get(pref::release_graphics_after);
  release_.selected = static_cast<int>(release_seconds.size()) - 1;
  if (seconds < 0) return;
  for (std::size_t i = 0; i + 1 < release_seconds.size(); ++i) {
    if (release_seconds[i] <= seconds) release_.selected = static_cast<int>(i);
  }
}

float SystemTab::content_height() const {
  if (flows("settings_system")) return flow_height_;
  return reset_top(ui_.prefs->get(pref::log_enabled)) + buttons_h + settings_bottom_pad;
}

void SystemTab::paint(Painter& p, Rect area) {
  if (const skin::Screen* screen = ui_.pack_screen("settings_system")) {
    paint_pack(p, *screen, area);
    return;
  }
  if (!ui_.skin().scales_stage()) {
    paint_luna(p, area);
    return;
  }
  const Theme& t = ui_.theme;
  const Prefs& prefs = *ui_.prefs;
  paint_heading(p, t, area, 25, tr().system_heading);
  paint_toggle(ui_, this, p, area, row_top, tr().system_autostart, prefs.get(pref::start_with_system), Autostart, toggles_[0]);
  paint_toggle(ui_, this, p, area, row_top + row_step, tr().system_tray, prefs.get(pref::minimize_to_tray), Tray, toggles_[1]);
  paint_toggle(ui_, this, p, area, detail_top, tr().system_detail, prefs.get(pref::input_detail), Detail, toggles_[2], tr().system_detail_hint);

  paint_heading(p, t, area, advanced_top, tr().system_advanced);
  paint_row_label(p, t, area, release_top + 5, tr().system_release);
  sync_release();
  release_rect_ = {area.x + dropdown_x, area.y + release_top - 1, dropdown_w, dropdown_h};
  release_.paint_button(ui_, this, p, release_rect_);
  const bool logging = prefs.get(pref::log_enabled);
  paint_toggle(ui_, this, p, area, logging_top, tr().system_logging, logging, Logging, toggles_[3]);
  if (logging) {
    paint_text_button(ui_, this, p, {area.x + buttons_x, area.y + open_logs_top, buttons_w, buttons_h}, tr().system_open_logs, OpenLogs, open_logs_);
  }
  paint_text_button(ui_, this, p, {area.x + buttons_x, area.y + reset_top(logging), buttons_w, buttons_h}, tr().system_reset, Reset, reset_);
}

void SystemTab::paint_popups(Painter& p, Rect) { release_.paint_list(ui_, this, p, release_rect_); }

void SystemTab::before_click(const Surface* owner, int id) {
  if (owner != this || !release_.owns(id)) release_.open = false;
}

bool SystemTab::acts_on_press(int id) const { return id == release_.id_base; }

void SystemTab::click(int id, float, float, bool) {
  Prefs& prefs = *ui_.prefs;
  const auto flip = [&](pref::Bool key) { prefs.set(key, !prefs.get(key)); };
  if (id == Autostart) {
    flip(pref::start_with_system);
  } else if (id == Tray) {
    flip(pref::minimize_to_tray);
  } else if (id == Detail) {
    flip(pref::input_detail);
  } else if (id == Logging) {
    flip(pref::log_enabled);
  } else if (id == OpenLogs) {
    if (on_open_logs) on_open_logs();
  } else if (id == Reset) {
    Ui* ui = &ui_;
    ui_.dialogs->show({tr().system_reset_question, tr().system_reset_confirm, tr().cancel, false, [ui] { ui->prefs->reset(); }, nullptr});
  } else if (release_.owns(id)) {
    if (release_.click(id)) prefs.set(pref::release_graphics_after, release_seconds[static_cast<std::size_t>(release_.selected)]);
    if (release_.open) place_dropdown(ui_, release_, release_rect_);
  }
}

bool SystemTab::wheel(float x, float y, float delta) { return release_.wheel(ui_, x, y, delta); }

bool SystemTab::escape() {
  const bool closed = release_.open;
  release_.open = false;
  return closed;
}

bool SystemTab::animating() const {
  for (const ToggleAnim& toggle : toggles_) {
    if (toggle.running(ui_)) return true;
  }
  return open_logs_.running(ui_) || reset_.running(ui_);
}

}  // namespace crpp::ui
