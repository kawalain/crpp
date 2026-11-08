#include <algorithm>

#include "crpp/locale/locale.hpp"
#include "crpp/ui/dialogs.hpp"
#include "crpp/ui/settings_tabs.hpp"

namespace crpp::ui {

namespace {

constexpr std::array<int, 4> release_seconds{0, 10, 60, -1};  // the last: never

// Differs from upstream: no sleep-on-minimize or input log rows, and the Detail mode
// row replaces "키보드 문자열 구분". Its description is three lines where upstream's is
// one, so what follows moves down by what is left over: repositories_top is where upstream's
// time section would be, 254, with one row out and two lines in. Time and language are on
// the display tab. The Repositories are not upstream's either, and push the advanced section down.
constexpr float detail_top = row_top + row_step * 2;
constexpr float repositories_top = 274;
constexpr float repository_rows_top = repositories_top + 74, repository_step = 58;
constexpr float buttons_x = 381, buttons_w = 140, buttons_h = 38;
constexpr float address_w = 355;

float add_top(std::size_t rows) { return repository_rows_top + static_cast<float>(rows) * repository_step + 4; }
// Below the field and why the last address could not be added: the packs, when there are any.
float packs_top(float repositories_end) { return repositories_end + 24; }
float pack_row_top(float repositories_end, std::size_t i) { return packs_top(repositories_end) + 40 + static_cast<float>(i) * repository_step; }
float release_top(float advanced) { return advanced + 51; }  // not upstream's
float logging_top(float advanced) { return release_top(advanced) + row_step; }
float open_logs_top(float advanced) { return logging_top(advanced) + row_step; }
float reset_top(float advanced, bool logging) { return (logging ? open_logs_top(advanced) + buttons_h : logging_top(advanced) + 36) + 32; }

// Its first digits, four at a time.
std::string grouped(std::string_view digits) {
  std::string out;
  for (std::size_t i = 0; i < digits.size(); ++i) {
    if (i > 0 && i % 4 == 0) out += ' ';
    out += digits[i];
  }
  return out;
}

}  // namespace

SystemTab::SystemTab(Ui& ui) : SettingsTab(ui) {
  release_.id_base = ReleaseList;
  address_.max_length = 300;
}

void SystemTab::add() {
  std::string typed = narrow(address_.text());
  if (on_add_repository && !typed.empty() && adding_.state != Adding::State::Checking) on_add_repository(std::move(typed));
}

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

float SystemTab::advanced_top() const {
  const float repositories_end = add_top(repositories_.size()) + buttons_h + (adding_.state != Adding::State::None ? 30 : 0);
  return (packs_.empty() ? repositories_end : pack_row_top(repositories_end, packs_.size()) - 12) + 36;
}

float SystemTab::content_height() const {
  if (flows("settings_system")) return flow_height_;
  return reset_top(advanced_top(), ui_.prefs->get(pref::log_enabled)) + buttons_h + settings_bottom_pad;
}

std::string SystemTab::fingerprint_text(std::size_t index) const {
  return locale::fill(tr().system_repository_fingerprint, {grouped(repositories_[index].fingerprint)});
}

std::string SystemTab::status_text() const {
  if (adding_.state == Adding::State::Checking) return tr().system_repository_checking;
  if (adding_.state == Adding::State::Failed) return locale::fill(tr().system_repository_failed, {adding_.reason});
  return {};
}

void SystemTab::paint_address(Painter& p, Rect box, const TextStyle& style, std::span<const Color> colors) {
  const Color ink = colors[0], soft = colors[1], caret_ink = colors[2], selection = colors[3], back = colors[4];
  address_style_ = style;
  address_x_ = box.x;
  p.rt()->PushAxisAlignedClip(D2D1::RectF(box.x, box.y, box.right(), box.bottom()), D2D1_ANTIALIAS_MODE_ALIASED);
  const bool focused = ui_.focus == &address_;
  const auto draw_field = [&] {
    return address_.paint(p, {box.x - address_scroll_, box.y, box.w + address_scroll_, box.h}, style, ink, widen(tr().system_repository_placeholder), style, soft,
                          caret_ink, selection, focused, ui_.caret_on);
  };
  Rect caret = draw_field();
  const float caret_in_text = caret.x + address_scroll_ - box.x;
  float wanted = address_scroll_;
  if (caret_in_text - address_scroll_ > box.w - 2) wanted = caret_in_text - box.w + 2;
  else if (caret_in_text < address_scroll_) wanted = caret_in_text;
  if (wanted != address_scroll_) {
    address_scroll_ = wanted;
    p.fill_rect(box, back);
    caret = draw_field();
  }
  p.rt()->PopAxisAlignedClip();
  if (focused) ui_.caret = ui_.to_window(caret);
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

  // As the Cloud pack draws them, so that its places are the same.
  paint_heading(p, t, area, repositories_top, tr().system_repositories);
  const TextStyle hint{.size = 14, .line_height = 20, .wrap = true}, label{.size = 18, .line_height = 26};
  p.text(tr().system_repositories_hint, {area.x + 16, area.y + repositories_top + 40, 505, 24}, hint, t.hl_text_soft);
  remove_.resize(repositories_.size());
  for (std::size_t i = 0; i < repositories_.size(); ++i) {
    const float top = area.y + repository_rows_top + static_cast<float>(i) * repository_step;
    p.text(repositories_[i].address, {area.x + 16, top, address_w, 26}, label, t.hl_text);
    p.text(fingerprint_text(i), {area.x + 16, top + 26, address_w, 20}, hint, t.hl_text_soft);
    if (!repositories_[i].official) {
      paint_text_button(ui_, this, p, {area.x + buttons_x, top + 4, buttons_w, buttons_h}, tr().system_repository_remove, RemoveRepository + static_cast<int>(i), remove_[i]);
    }
  }
  const Rect box{area.x + 16, area.y + add_top(repositories_.size()), address_w, buttons_h};
  p.fill_round(box, 10, t.hl_pale);
  ui_.add_hit(this, box, Address);
  const Color colors[] = {t.hl_text, t.hl_text_soft, t.hl_text, with_alpha(t.hl, 0.4f), t.hl_pale};
  paint_address(p, {box.x + 12, box.y + 6, address_w - 24, 26}, label, colors);
  paint_text_button(ui_, this, p, {area.x + buttons_x, box.y, buttons_w, buttons_h}, tr().system_repository_add, AddRepository, add_);
  if (adding_.state != Adding::State::None) {
    p.text(status_text(), {area.x + 16, box.y + 44, 505, 22}, hint, adding_.state == Adding::State::Failed ? t.error_text : t.hl_text_soft);
  }
  const float repositories_end = box.y - area.y + buttons_h + (adding_.state != Adding::State::None ? 30 : 0);
  if (!packs_.empty()) paint_subheading(p, t, area, packs_top(repositories_end), tr().system_packs);
  remove_pack_.resize(packs_.size());
  for (std::size_t i = 0; i < packs_.size(); ++i) {
    const float top = area.y + pack_row_top(repositories_end, i);
    p.text(packs_[i].name, {area.x + 16, top, address_w, 26}, label, t.hl_text);
    p.text(packs_[i].detail, {area.x + 16, top + 26, address_w, 20}, hint, t.hl_text_soft);
    if (packs_[i].removable) {
      paint_text_button(ui_, this, p, {area.x + buttons_x, top + 4, buttons_w, buttons_h}, tr().system_pack_remove, RemovePack + static_cast<int>(i), remove_pack_[i]);
    }
  }

  const float advanced = advanced_top();
  paint_heading(p, t, area, advanced, tr().system_advanced);
  paint_row_label(p, t, area, release_top(advanced) + 5, tr().system_release);
  sync_release();
  release_rect_ = {area.x + dropdown_x, area.y + release_top(advanced) - 1, dropdown_w, dropdown_h};
  release_.paint_button(ui_, this, p, release_rect_);
  const bool logging = prefs.get(pref::log_enabled);
  paint_toggle(ui_, this, p, area, logging_top(advanced), tr().system_logging, logging, Logging, toggles_[3]);
  if (logging) {
    paint_text_button(ui_, this, p, {area.x + buttons_x, area.y + open_logs_top(advanced), buttons_w, buttons_h}, tr().system_open_logs, OpenLogs, open_logs_);
  }
  paint_text_button(ui_, this, p, {area.x + buttons_x, area.y + reset_top(advanced, logging), buttons_w, buttons_h}, tr().system_reset, Reset, reset_);
}

void SystemTab::paint_popups(Painter& p, Rect) { release_.paint_list(ui_, this, p, release_rect_); }

void SystemTab::before_click(const Surface* owner, int id) {
  if (owner != this || !release_.owns(id)) release_.open = false;
}

bool SystemTab::acts_on_press(int id) const { return id == release_.id_base || id == Address; }

void SystemTab::click(int id, float x, float, bool shift) {
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
  } else if (id == Address) {
    ui_.set_focus(this, &address_);
    address_.click_at(ui_.gfx, address_style_, ui_.to_stage_x(x) - address_x_ + address_scroll_, shift);
  } else if (id == AddRepository) {
    add();
  } else if (id >= RemovePack && static_cast<std::size_t>(id - RemovePack) < packs_.size()) {
    if (on_remove_pack && packs_[static_cast<std::size_t>(id - RemovePack)].removable) on_remove_pack(static_cast<std::size_t>(id - RemovePack));
  } else if (id >= RemoveRepository && static_cast<std::size_t>(id - RemoveRepository) < repositories_.size()) {
    if (on_remove_repository) on_remove_repository(static_cast<std::size_t>(id - RemoveRepository));
  } else if (id == Reset) {
    Ui* ui = &ui_;
    ui_.dialogs->show({tr().system_reset_question, tr().system_reset_confirm, tr().cancel, false, [ui] { ui->prefs->reset(); }, nullptr});
  } else if (release_.owns(id)) {
    if (release_.click(id)) prefs.set(pref::release_graphics_after, release_seconds[static_cast<std::size_t>(release_.selected)]);
    if (release_.open) place_dropdown(ui_, release_, release_rect_);
  }
}

void SystemTab::drag(float x, float) {
  address_.click_at(ui_.gfx, address_style_, ui_.to_stage_x(x) - address_x_ + address_scroll_, true);
}

void SystemTab::enter(bool) { add(); }

Cursor SystemTab::cursor(int id) const { return id == Address ? Cursor::IBeam : Cursor::Hand; }

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
  return open_logs_.running(ui_) || reset_.running(ui_) || add_.running(ui_) ||
         std::ranges::any_of(remove_, [&](const Fade& fade) { return fade.running(ui_); }) ||
         std::ranges::any_of(remove_pack_, [&](const Fade& fade) { return fade.running(ui_); });
}

}  // namespace crpp::ui
