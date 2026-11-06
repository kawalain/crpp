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
constexpr float advanced_top = time_top + 166;
constexpr float logging_top = time_top + 217;
constexpr float open_logs_top = logging_top + row_step;
constexpr float buttons_x = 381, buttons_w = 140, buttons_h = 38;

constexpr std::string_view detail_hint =
    "Space, Enter, Shift 같은 특수 키를 구분해 보내 소리가 다양해집니다.\n"
    "대신 단어의 경계와 줄바꿈이 다른 사람에게 드러납니다.\n"
    "글자와 숫자 키는 켜도 구분되지 않습니다.";

float reset_top(bool logging) { return (logging ? open_logs_top + buttons_h : logging_top + 36) + 32; }

}  // namespace

SystemTab::SystemTab(Ui& ui) : SettingsTab(ui) {
  idle_.options = {"15분", "30분", "1시간"};
  idle_.id_base = IdleList;
}

float SystemTab::content_height() const {
  return reset_top(ui_.prefs->get(pref::log_enabled)) + buttons_h + settings_bottom_pad;
}

void SystemTab::paint(Painter& p, Rect area) {
  const Theme& t = ui_.theme;
  const Prefs& prefs = *ui_.prefs;
  paint_heading(p, t, area, 25, "시스템");
  paint_toggle(ui_, this, p, area, row_top, "시스템과 함께 자동 실행", prefs.get(pref::start_with_system), Autostart, toggles_[0]);
  paint_toggle(ui_, this, p, area, row_top + row_step, "시스템 트레이에 최소화", prefs.get(pref::minimize_to_tray), Tray, toggles_[1]);
  paint_toggle(ui_, this, p, area, detail_top, "입력 상세 모드", prefs.get(pref::input_detail), Detail, toggles_[2], detail_hint);

  paint_heading(p, t, area, time_top, "시간");
  paint_toggle(ui_, this, p, area, time_top + 51, "24시간제", prefs.get(pref::use_24h), Use24h, toggles_[3]);
  const float idle_top = time_top + 51 + row_step;
  paint_row_label(p, t, area, idle_top + 5, "자리비움 타이머");
  const int minutes = prefs.get(pref::idle_minutes);
  idle_.selected = 1;  // upstream takes anything else as 30 minutes
  for (std::size_t i = 0; i < idle_minutes.size(); ++i) {
    if (idle_minutes[i] == minutes) idle_.selected = static_cast<int>(i);
  }
  idle_rect_ = {area.x + dropdown_x, area.y + idle_top - 1, dropdown_w, dropdown_h};
  idle_.paint_button(ui_, this, p, idle_rect_);

  paint_heading(p, t, area, advanced_top, "고급");
  const bool logging = prefs.get(pref::log_enabled);
  paint_toggle(ui_, this, p, area, logging_top, "로그 기록", logging, Logging, toggles_[4]);
  if (logging) {
    paint_text_button(ui_, this, p, {area.x + buttons_x, area.y + open_logs_top, buttons_w, buttons_h}, "로그 폴더 열기", OpenLogs, open_logs_);
  }
  paint_text_button(ui_, this, p, {area.x + buttons_x, area.y + reset_top(logging), buttons_w, buttons_h}, "설정 초기화", Reset, reset_);
}

void SystemTab::paint_popups(Painter& p, Rect) { idle_.paint_list(ui_, this, p, idle_rect_); }

void SystemTab::before_click(const Surface* owner, int id) {
  if (owner != this || !idle_.owns(id)) idle_.open = false;
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
    ui_.dialogs->show({"설정에서 변경한 모든 값을\n초기화 할까요?", "초기화", "취소", false, [ui] { ui->prefs->reset(); }, nullptr});
  } else if (idle_.owns(id)) {
    if (idle_.click(id)) prefs.set(pref::idle_minutes, idle_minutes[static_cast<std::size_t>(idle_.selected)]);
    if (idle_.open) place_dropdown(idle_, idle_rect_);
  }
}

bool SystemTab::wheel(float x, float y, float delta) { return idle_.wheel(ui_, x, y, delta); }

bool SystemTab::escape() {
  const bool closed = idle_.open;
  idle_.open = false;
  return closed;
}

bool SystemTab::animating() const {
  for (const ToggleAnim& toggle : toggles_) {
    if (toggle.running(ui_)) return true;
  }
  return open_logs_.running(ui_) || reset_.running(ui_);
}

}  // namespace crpp::ui
