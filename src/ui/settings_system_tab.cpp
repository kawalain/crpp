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
  server_.max_length = 300;
}

void SystemTab::apply_server() {
  if (!on_apply_server || server_state_.state == ServerState::State::Checking) return;
  on_apply_server(narrow(server_.text()));
}

std::string SystemTab::server_text() const {
  if (server_state_.state == ServerState::State::Checking) return tr().server_checking;
  if (server_state_.state == ServerState::State::Failed) return locale::fill(tr().server_failed, {server_state_.reason});
  return locale::fill(tr().system_server_now, {server_state_.current});
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
  const bool focused = ui_.focus == &address_;
  const bool held = ui_.pressed_owner == this && ui_.pressed_id == Address;
  const Rect caret = address_line_.paint(ui_, p, address_, box, style, widen(tr().system_repository_placeholder), colors, focused, held);
  if (focused) ui_.caret = ui_.to_window(caret);
}

void SystemTab::paint_server(Painter& p, Rect box, const TextStyle& style, std::span<const Color> colors) {
  const bool focused = ui_.focus == &server_;
  const bool held = ui_.pressed_owner == this && ui_.pressed_id == Server;
  const Rect caret = server_line_.paint(ui_, p, server_, box, style, widen(tr().account_server_placeholder), colors, focused, held);
  if (focused) ui_.caret = ui_.to_window(caret);
}

void SystemTab::paint(Painter& p, Rect area) {
  if (const skin::Screen* screen = ui_.pack_screen("settings_system")) paint_pack(p, *screen, area);
}

void SystemTab::paint_popups(Painter& p, Rect) { release_.paint_list(ui_, this, p, release_rect_); }

void SystemTab::before_click(const Surface* owner, int id) {
  if (owner != this || !release_.owns(id)) release_.open = false;
}

bool SystemTab::acts_on_press(int id) const { return id == release_.id_base || id == Address || id == Server || is_fingerprint(id); }

void SystemTab::paint_fingerprint(Painter& p, std::size_t row, Rect box, const TextStyle& style, Color ink, Color selection) {
  if (fingerprints_.size() != repositories_.size()) fingerprints_.resize(repositories_.size());
  FingerprintLine& line = fingerprints_[row];
  line.text = widen(fingerprint_text(row));
  line.box = box;
  fingerprint_style_ = style;
  const auto layout = ui_.gfx.layout(line.text, style, box.w, box.h);
  if (!layout) return;
  ui_.add_hit(this, box, Fingerprint + static_cast<int>(row));
  if (const auto part = selection_.in_block(row, line.text.size())) {
    p.fill_text_range(layout.get(), box.x, box.y, part->first, part->second, selection);
  }
  p.draw_layout(layout.get(), box.x, box.y, ink);
}

std::wstring_view SystemTab::fingerprint_of(std::size_t row) const {
  return row < fingerprints_.size() ? std::wstring_view(fingerprints_[row].text) : std::wstring_view{};
}

std::wstring SystemTab::selected_text() const {
  return selection_.text([this](std::size_t n) { return fingerprint_of(n); });
}

TextPoint SystemTab::fingerprint_point(float x, float y) const {
  const float sx = ui_.to_stage_x(x), sy = ui_.to_stage_y(y);
  // The last line that starts above the point.
  std::size_t row = 0;
  for (std::size_t i = 0; i < fingerprints_.size(); ++i) {
    if (fingerprints_[i].box.y <= sy) row = i;
  }
  if (row >= fingerprints_.size()) return {};
  const FingerprintLine& line = fingerprints_[row];
  if (sy >= line.box.bottom() && row + 1 < fingerprints_.size()) return {row, line.text.size()};
  const auto layout = ui_.gfx.layout(line.text, fingerprint_style_, line.box.w, line.box.h);
  if (!layout) return {row, 0};
  BOOL trailing = FALSE, inside = FALSE;
  DWRITE_HIT_TEST_METRICS m{};
  layout->HitTestPoint(sx - line.box.x, sy - line.box.y, &trailing, &inside, &m);
  return {row, std::min<std::size_t>(m.textPosition + (trailing ? m.length : 0), line.text.size())};
}

void SystemTab::focus(int id) {
  if (id == Address) ui_.set_focus(this, &address_);
  if (id == Server) ui_.set_focus(this, &server_);
}

std::vector<MenuItem> SystemTab::context_menu(int id, float x, float y) {
  if (id == Address || id == Server) {
    TextField& field = id == Address ? address_ : server_;
    LineBox& line = id == Address ? address_line_ : server_line_;
    menu_field_ = &field;
    ui_.set_focus(this, &field);
    field.right_press(line.position_at(ui_, field, ui_.to_stage_x(x)));
    return ui_.field_menu(field);
  }
  menu_field_ = id == -1 && (ui_.focus == &address_ || ui_.focus == &server_) ? static_cast<TextField*>(ui_.focus) : nullptr;
  if (menu_field_) return ui_.field_menu(*menu_field_);
  if (is_fingerprint(id)) {
    // Outside what is selected, the selection goes, as in a browser.
    const TextPoint at = fingerprint_point(x, y);
    if (selection_.empty() || at < selection_.start() || selection_.end() < at) selection_.clear();
    ui_.selection_owner = this;
  } else if (id != -1) {
    return {};
  }
  return {{MenuCommand::Copy, !selection_.empty()}, {MenuCommand::SelectAll, !fingerprints_.empty()}};
}

void SystemTab::menu_command(MenuCommand command) {
  if (menu_field_) {
    ui_.set_focus(this, menu_field_);
    ui_.field_command(*menu_field_, command);
  } else if (command == MenuCommand::Copy) {
    if (!selection_.empty()) ui_.copy(selected_text());
  } else if (command == MenuCommand::SelectAll && !fingerprints_.empty()) {
    ui_.selection_owner = this;
    selection_.select({0, 0}, {fingerprints_.size() - 1, fingerprints_.back().text.size()});
  }
}

bool SystemTab::key(unsigned vk, bool ctrl, bool shift) {
  if (ui_.focus == &address_ || ui_.focus == &server_) return Surface::key(vk, ctrl, shift);
  if (!ctrl || shift) return false;
  if (vk == 'C' || vk == VK_INSERT) {
    if (!selection_.empty()) ui_.copy(selected_text());
    return true;
  }
  if (vk == 'A' && !fingerprints_.empty()) {
    selection_.select({0, 0}, {fingerprints_.size() - 1, fingerprints_.back().text.size()});
    return true;
  }
  return false;
}

const Dropdown* SystemTab::dropdown_of(int id) const {
  for (const Dropdown* box : {&release_}) {
    if (box->owns(id)) return box;
  }
  return nullptr;
}

bool SystemTab::stop_key(int id, unsigned vk, bool ctrl, bool, bool alt) {
  if (ctrl) return false;
    if (release_.owns(id)) {
      const std::optional<int> hit = release_.key(ui_, this, vk, alt);
      if (!hit) return false;
      if (*hit != Dropdown::taken_alone) click(*hit, 0, 0, false);
      return true;
    }
  return false;
}

void SystemTab::click(int id, float x, float y, bool shift) {
  if (is_fingerprint(id)) {
    // The fingerprints take the keys now: Ctrl+C copies what is selected of them.
    ui_.selection_owner = this;
    ui_.capture = this;
    selecting_ = true;
    selection_.press(fingerprint_point(x, y), ui_.clicks, shift, [this](std::size_t n) { return fingerprint_of(n); });
    return;
  }
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
    address_.press(address_line_.position_at(ui_, address_, ui_.to_stage_x(x)), ui_.clicks, shift);
  } else if (id == AddRepository) {
    add();
  } else if (id == Server) {
    ui_.set_focus(this, &server_);
    server_.press(server_line_.position_at(ui_, server_, ui_.to_stage_x(x)), ui_.clicks, shift);
  } else if (id == ApplyServer) {
    apply_server();
  } else if (id >= OpenRepository && static_cast<std::size_t>(id - OpenRepository) < repositories_.size()) {
    if (on_open_repository) on_open_repository(repositories_[static_cast<std::size_t>(id - OpenRepository)].address);
  } else if (id >= UpdatePack && static_cast<std::size_t>(id - UpdatePack) < packs_.size()) {
    if (on_update_pack && packs_[static_cast<std::size_t>(id - UpdatePack)].held) on_update_pack(static_cast<std::size_t>(id - UpdatePack));
  } else if (id >= RemovePack && static_cast<std::size_t>(id - RemovePack) < packs_.size()) {
    if (on_remove_pack && packs_[static_cast<std::size_t>(id - RemovePack)].removable) on_remove_pack(static_cast<std::size_t>(id - RemovePack));
  } else if (id >= RemoveRepository && static_cast<std::size_t>(id - RemoveRepository) < repositories_.size()) {
    if (on_remove_repository) on_remove_repository(static_cast<std::size_t>(id - RemoveRepository));
  } else if (id == Reset) {
    Ui* ui = &ui_;
    ui_.dialogs->show({tr().system_reset_question, tr().system_reset_confirm, tr().cancel, false, [ui] { ui->prefs->reset(); }, nullptr, true});
  } else if (release_.owns(id)) {
    if (release_.click(id)) prefs.set(pref::release_graphics_after, release_seconds[static_cast<std::size_t>(release_.selected)]);
    if (release_.open) place_dropdown(ui_, release_, release_rect_);
  }
}

void SystemTab::drag(float x, float y) {
  if (selecting_) {
    selection_.drag_to(fingerprint_point(x, y), [this](std::size_t n) { return fingerprint_of(n); });
    return;
  }
  if (ui_.focus == &server_) server_line_.drag(ui_, server_, ui_.to_stage_x(x));
  else address_line_.drag(ui_, address_, ui_.to_stage_x(x));
}

void SystemTab::enter(bool) {
  if (ui_.focus == &server_) apply_server();
  else add();
}

Cursor SystemTab::cursor(int id) const { return id == Address || id == Server || is_fingerprint(id) ? Cursor::IBeam : Cursor::Hand; }

bool SystemTab::wheel(float x, float y, float delta) { return release_.wheel(ui_, x, y, delta); }

bool SystemTab::escape() {
  const bool closed = release_.open;
  release_.open = false;
  return closed;
}

}  // namespace crpp::ui
