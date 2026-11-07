#include <algorithm>

#include "crpp/ui/dialogs.hpp"
#include "crpp/ui/settings_tabs.hpp"

namespace crpp::ui {

namespace {

constexpr std::array<int, 3> idle_minutes{15, 30, 60};  // IDLE_CHOICES

// Differs from upstream: no sleep-on-minimize or input log rows, and the Detail mode
// row replaces "키보드 문자열 구분". Its description is three lines where upstream's is
// one, so everything below moves down by what is left over: time_top is upstream's
// 254 - shift with one row out and two lines in.
constexpr float detail_top = row_top + row_step * 2;
constexpr float time_top = 274;
constexpr float language_top = time_top + 166;
constexpr float advanced_top = language_top + 114;
constexpr float logging_top = advanced_top + 51;
constexpr float open_logs_top = logging_top + row_step;
constexpr float buttons_x = 381, buttons_w = 140, buttons_h = 38;

// Saved as a Locale's id, and listed after the entry for following the OS.
int selected_language(const std::string& id) {
  const auto locales = locale::all_locales();
  const auto found = std::ranges::find(locales, std::string_view(id), &locale::Locale::id);
  return found == locales.end() ? 0 : static_cast<int>(found - locales.begin()) + 1;
}

float reset_top(bool logging) { return (logging ? open_logs_top + buttons_h : logging_top + 36) + 32; }

}  // namespace

SystemTab::SystemTab(Ui& ui) : SettingsTab(ui) {
  idle_.id_base = IdleList;
  language_.id_base = LanguageList;
}

float SystemTab::content_height() const {
  return reset_top(ui_.prefs->get(pref::log_enabled)) + buttons_h + settings_bottom_pad;
}

void SystemTab::paint(Painter& p, Rect area) {
  const Theme& t = ui_.theme;
  const Prefs& prefs = *ui_.prefs;
  idle_.options = {tr().system_idle_15, tr().system_idle_30, tr().system_idle_60};
  paint_heading(p, t, area, 25, tr().system_heading);
  paint_toggle(ui_, this, p, area, row_top, tr().system_autostart, prefs.get(pref::start_with_system), Autostart, toggles_[0]);
  paint_toggle(ui_, this, p, area, row_top + row_step, tr().system_tray, prefs.get(pref::minimize_to_tray), Tray, toggles_[1]);
  paint_toggle(ui_, this, p, area, detail_top, tr().system_detail, prefs.get(pref::input_detail), Detail, toggles_[2], tr().system_detail_hint);

  paint_heading(p, t, area, time_top, tr().system_time);
  paint_toggle(ui_, this, p, area, time_top + 51, tr().system_24h, prefs.get(pref::use_24h), Use24h, toggles_[3]);
  const float idle_top = time_top + 51 + row_step;
  paint_row_label(p, t, area, idle_top + 5, tr().system_idle);
  const int minutes = prefs.get(pref::idle_minutes);
  idle_.selected = 1;  // upstream takes anything else as 30 minutes
  for (std::size_t i = 0; i < idle_minutes.size(); ++i) {
    if (idle_minutes[i] == minutes) idle_.selected = static_cast<int>(i);
  }
  idle_rect_ = {area.x + dropdown_x, area.y + idle_top - 1, dropdown_w, dropdown_h};
  idle_.paint_button(ui_, this, p, idle_rect_);

  paint_heading(p, t, area, language_top, tr().system_language);
  paint_row_label(p, t, area, language_top + 51 + 5, tr().system_language_row);
  language_.options = {tr().system_language_auto};
  for (const locale::Locale& l : locale::all_locales()) language_.options.emplace_back(l.name);
  language_.selected = selected_language(prefs.get(pref::language));
  language_rect_ = {area.x + dropdown_x, area.y + language_top + 51 - 1, dropdown_w, dropdown_h};
  language_.paint_button(ui_, this, p, language_rect_);

  paint_heading(p, t, area, advanced_top, tr().system_advanced);
  const bool logging = prefs.get(pref::log_enabled);
  paint_toggle(ui_, this, p, area, logging_top, tr().system_logging, logging, Logging, toggles_[4]);
  if (logging) {
    paint_text_button(ui_, this, p, {area.x + buttons_x, area.y + open_logs_top, buttons_w, buttons_h}, tr().system_open_logs, OpenLogs, open_logs_);
  }
  paint_text_button(ui_, this, p, {area.x + buttons_x, area.y + reset_top(logging), buttons_w, buttons_h}, tr().system_reset, Reset, reset_);
}

void SystemTab::paint_popups(Painter& p, Rect) {
  idle_.paint_list(ui_, this, p, idle_rect_);
  language_.paint_list(ui_, this, p, language_rect_);
}

void SystemTab::before_click(const Surface* owner, int id) {
  if (owner != this || !idle_.owns(id)) idle_.open = false;
  if (owner != this || !language_.owns(id)) language_.open = false;
}

void SystemTab::click(int id, float, float, bool) {
  Prefs& prefs = *ui_.prefs;
  const auto flip = [&](pref::Bool key) { prefs.set(key, !prefs.get(key)); };
  if (id == Autostart) {
    flip(pref::start_with_system);
  } else if (id == Tray) {
    flip(pref::minimize_to_tray);
  } else if (id == Detail) {
    flip(pref::input_detail);
  } else if (id == Use24h) {
    flip(pref::use_24h);
  } else if (id == Logging) {
    flip(pref::log_enabled);
  } else if (id == OpenLogs) {
    if (on_open_logs) on_open_logs();
  } else if (id == Reset) {
    Ui* ui = &ui_;
    ui_.dialogs->show({tr().system_reset_question, tr().system_reset_confirm, tr().cancel, false, [ui] { ui->prefs->reset(); }, nullptr});
  } else if (idle_.owns(id)) {
    if (idle_.click(id)) prefs.set(pref::idle_minutes, idle_minutes[static_cast<std::size_t>(idle_.selected)]);
    if (idle_.open) place_dropdown(idle_, idle_rect_);
  } else if (language_.owns(id)) {
    if (language_.click(id)) {
      const auto locales = locale::all_locales();
      prefs.set(pref::language, language_.selected == 0 ? std::string_view{} : locales[static_cast<std::size_t>(language_.selected - 1)].id);
    }
    if (language_.open) place_dropdown(language_, language_rect_);
  }
}

bool SystemTab::wheel(float x, float y, float delta) { return idle_.wheel(ui_, x, y, delta) || language_.wheel(ui_, x, y, delta); }

bool SystemTab::escape() {
  const bool closed = idle_.open || language_.open;
  idle_.open = language_.open = false;
  return closed;
}

bool SystemTab::animating() const {
  for (const ToggleAnim& toggle : toggles_) {
    if (toggle.running(ui_)) return true;
  }
  return open_logs_.running(ui_) || reset_.running(ui_);
}

}  // namespace crpp::ui
