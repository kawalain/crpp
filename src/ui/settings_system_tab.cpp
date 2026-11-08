#include <algorithm>

#include "crpp/locale/locale.hpp"
#include "crpp/ui/dialogs.hpp"
#include "crpp/ui/settings_tabs.hpp"

namespace crpp::ui {

namespace {

constexpr std::array<int, 4> release_seconds{0, 10, 60, -1};  // the last: never

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
  if (const skin::Screen* screen = ui_.pack_screen("settings_system")) paint_pack(p, *screen, area);
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
    address_.press(address_.position_at(ui_.gfx, address_style_, ui_.to_stage_x(x) - address_x_ + address_scroll_, 0), ui_.clicks, shift);
  } else if (id == AddRepository) {
    add();
  } else if (id >= UpdatePack && static_cast<std::size_t>(id - UpdatePack) < packs_.size()) {
    if (on_update_pack && packs_[static_cast<std::size_t>(id - UpdatePack)].held) on_update_pack(static_cast<std::size_t>(id - UpdatePack));
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
  address_.drag_to(address_.position_at(ui_.gfx, address_style_, ui_.to_stage_x(x) - address_x_ + address_scroll_, 0));
}

void SystemTab::enter(bool) { add(); }

Cursor SystemTab::cursor(int id) const { return id == Address ? Cursor::IBeam : Cursor::Hand; }

bool SystemTab::wheel(float x, float y, float delta) { return release_.wheel(ui_, x, y, delta); }

bool SystemTab::escape() {
  const bool closed = release_.open;
  release_.open = false;
  return closed;
}

}  // namespace crpp::ui
