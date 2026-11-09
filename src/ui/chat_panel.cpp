#include "crpp/ui/chat_panel.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/toasts.hpp"

namespace crpp::ui {

std::string chat_clock_text(long long ms, bool use_24h) {
  const std::time_t t = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
  localtime_s(&tm, &t);
  const std::time_t now = static_cast<std::time_t>(wall_clock_seconds());
  std::tm today{};
  localtime_s(&today, &now);
  const auto day_index = [](const std::tm& d) {
    std::tm copy = d;
    copy.tm_hour = copy.tm_min = copy.tm_sec = 0;
    return static_cast<long long>(std::mktime(&copy)) / 86400;
  };
  const std::string clock = locale::format_clock(tm.tm_hour, tm.tm_min, use_24h);
  const long long days = day_index(today) - day_index(tm);
  if (days <= 0) return clock;
  if (days == 1) return locale::fill(tr().chat_yesterday, {clock});
  if (days == 2) return locale::fill(tr().chat_two_days_ago, {clock});
  char month[4], day[4];
  std::snprintf(month, sizeof month, "%02d", tm.tm_mon + 1);
  std::snprintf(day, sizeof day, "%02d", tm.tm_mday);
  return locale::fill(tr().chat_earlier, {month, day, clock});
}

namespace {

bool chat_grouped(const ChatEntry& e, const ChatEntry* prev) {
  if (!prev || prev->slot != e.slot || prev->cid != e.cid) return false;
  const long long gap = e.time_ms - prev->time_ms;
  return gap >= 0 && gap <= 5 * 60 * 1000;
}

}  // namespace

ChatPanel::ChatPanel(Ui& ui) : Surface(ui) {
  input_.max_length = max_message;
  input_.multiline = true;
}

void ChatPanel::add(ChatEntry entry) {
  const bool new_line = away_ && !new_from_ && entry.slot != my_slot_;
  if (entry.slot != my_slot_ && mentions(entry.text, names_)) ui_.achieve(AchievementId::Named);
  log_.push_back(std::move(entry));
  if (new_line) {
    new_from_ = dropped_ + log_.size() - 1;
    new_seen_ = false;
  }
  if (log_.size() > 1000) {
    log_.erase(log_.begin());
    ++dropped_;
    if (first_ > 0) --first_;
    else dirty_ = true;  // the first shown one is gone
  }
}

void ChatPanel::set_names(std::vector<std::string> names) {
  if (names == names_) return;
  names_ = std::move(names);
  dirty_ = true;
}

void ChatPanel::set_away(bool away) {
  if (away == away_) return;
  away_ = away;
  if (!away) {
    if (new_from_) new_seen_ = true;
  } else if (new_seen_) {
    forget_new_line();
  }
}

void ChatPanel::forget_new_line() {
  if (!new_from_) return;
  new_from_.reset();
  new_seen_ = false;
  dirty_ = true;  // the rows are laid out without it
}

void ChatPanel::clear() {
  new_from_.reset();
  new_seen_ = false;
  dropped_ += log_.size();
  log_.clear();
  rows_.clear();
  log_selection_.clear();
  first_ = 0;
  fresh_ = dirty_ = true;
  scroll_ = {};
  stick_ = true;
  input_.clear();
  limiter_.reset();
  last_sent_.clear();
}

void ChatPanel::send() {
  std::wstring text = input_.text();
  const auto blank = [](wchar_t c) { return c == L' ' || c == 0x0A; };
  while (!text.empty() && blank(text.back())) text.pop_back();
  std::size_t lead = 0;
  while (lead < text.size() && blank(text[lead])) ++lead;
  text.erase(0, lead);
  if (text.empty()) return;
  std::string message = narrow(text);
  if (!limiter_.try_send(clock_ms())) {
    // Still sent: every attempt prolongs the block on the server too, and its answer
    // keeps the two in step. The text stays in the box.
    shake();
    if (on_send) on_send(std::move(message));
    return;
  }
  last_sent_ = message;
  if (on_send && !on_send(std::move(message))) {
    if (ui_.toasts) ui_.toasts->toast(tr().chat_send_failed);
    return;
  }
  ui_.tally(Tally::Messages, 1);
  forget_new_line();
  if (history_.empty() || history_.back() != text) history_.push_back(text);
  if (history_.size() > history_size) history_.erase(history_.begin());
  history_at_ = -1;
  draft_.clear();
  input_.clear();
}

bool ChatPanel::recall(int dir) {
  const int last = static_cast<int>(history_.size()) - 1;
  const int to = history_at_ + (dir < 0 ? 1 : -1);
  if (to > last || to < -1) return false;
  if (history_at_ == -1) draft_ = input_.text();
  history_at_ = to;
  input_.set_text(to == -1 ? draft_ : history_[static_cast<std::size_t>(last - to)]);
  return true;
}

void ChatPanel::blocked(std::uint64_t ms) {
  if (!limiter_.server_blocked(clock_ms(), ms)) return;
  if (limiter_.blocks_in_a_row() >= ChatLimiter::firm_from) ui_.achieve(AchievementId::CalmFirm);
  shake();
  if (input_.text().empty() && !last_sent_.empty()) input_.set_text(widen(last_sent_));
}

void ChatPanel::shake() { ++shakes_; }

bool ChatPanel::animating() const { return link_tip_.animating(ui_); }  // what its screen moves asks for itself

bool ChatPanel::tick(unsigned long long now_ms) {
  const bool moving = animating();
  return limiter_.expire(now_ms) || moving;
}

Cursor ChatPanel::cursor(int id) const {
  if (id == Input || id == Text) return Cursor::IBeam;
  return id == Area ? Cursor::Arrow : Cursor::Hand;
}

void ChatPanel::paint(Painter& p, Rect panel) {
  if (const skin::Screen* screen = ui_.pack_screen("chat")) paint_pack(p, *screen, panel);
}

bool ChatPanel::chat_grouped_with_previous(std::size_t index) const { return chat_grouped(log_[index], index > first_ ? &log_[index - 1] : nullptr); }

void ChatPanel::click(int id, float x, float y, bool shift) {
  if (id == BarUp || id == BarDown || id == BarTrack) {
    scroll_.step_towards(bar_track_, y);
    scrolled();
    return;
  }
  if (id == BarThumb) {
    ui_.capture = this;
    grab_y_ = y;
    grab_offset_ = scroll_.offset;
    return;
  }
  if (id >= LinkBase) {
    const auto number = static_cast<std::size_t>(id - LinkBase) / links_per_entry;
    const auto link = static_cast<std::size_t>(id - LinkBase) % links_per_entry;
    if (number < dropped_ || number - dropped_ >= log_.size()) return;
    // From the message itself: until the next paint rows_ may not match log_, and this
    // address goes to the shell.
    const std::wstring text = widen(log_[number - dropped_].text);
    const std::vector<LinkSpan> links = find_links(text);
    if (link >= links.size()) return;
    const std::string url = narrow(text.substr(links[link].start, links[link].length));
    if (on_open_link && !on_open_link(url) && ui_.toasts) ui_.toasts->toast(tr().chat_link_failed);
    return;
  }
  if (id == Text) {
    // The messages take the keys now: Ctrl+C copies what is selected, Ctrl+A selects them all.
    ui_.selection_owner = this;
    ui_.capture = this;
    selecting_log_ = true;
    drag_x_ = x;
    drag_y_ = y;
    scrolled_at_ = 0;
    log_selection_.press(log_point(x, y), ui_.clicks, shift, [this](std::size_t n) { return text_of(n); });
    return;
  }
  if (id != Input && id != Send) return;
  log_selection_.clear();
  if (ui_.selection_owner == this) ui_.selection_owner = nullptr;
  ui_.set_focus(this, &input_);
  if (id == Send) {
    send();
    return;
  }
  ui_.capture = this;
  selecting_ = true;
  input_.press(input_position(x, y), ui_.clicks, shift);
}

std::size_t ChatPanel::input_position(float x, float y) const {
  return input_.position_at(ui_.gfx, pack_input_style_, x - input_box_.x, y - input_box_.y, std::max(1.0f, input_text_w_));
}

void ChatPanel::focus(int id) {
  if (id == Input) ui_.set_focus(this, &input_);
  else if (id == Area) ui_.selection_owner = this;  // the messages take the keys, as a click on them gives them
}

bool ChatPanel::stop_key(int id, unsigned vk, bool ctrl, bool shift, bool) {
  if (id != Area || ctrl || log_.empty()) return false;
  // Only the messages laid out can be gone to: the page in view and those above it already brought in.
  const std::size_t first = dropped_ + first_, last = dropped_ + log_.size() - 1;
  const bool on_one = key_message_ >= first && key_message_ <= last;
  const std::size_t at = on_one ? key_message_ : last;
  std::size_t to = at;
  switch (vk) {
    case VK_UP: to = on_one && at > first ? at - 1 : at; break;
    case VK_DOWN: to = on_one && at < last ? at + 1 : at; break;
    case VK_PRIOR: to = at > first + 5 ? at - 5 : first; break;
    case VK_NEXT: to = std::min(at + 5, last); break;
    case VK_HOME: to = first; break;
    case VK_END: to = last; break;
    default: return false;
  }
  if (!shift || !on_one) key_from_ = to;
  key_message_ = to;
  const std::size_t lo = std::min(key_from_, to), hi = std::max(key_from_, to);
  log_selection_.select({lo, 0}, {hi, widen(log_[hi - dropped_].text).size()});
  ui_.selection_owner = this;
  // Its row comes into view, in the units the log scrolls by.
  const std::size_t row = to - dropped_ - first_;
  if (row < rows_.size()) {
    const Row& r = rows_[row];
    const float scale = log_geometry_.scale > 0 ? log_geometry_.scale : 1;
    const float top = (r.head ? r.y_head : r.y_text) * scale, bottom = (r.y_text + r.h_text) * scale;
    if (top < scroll_.target) scroll_.go_to(ui_, top);
    else if (bottom > scroll_.target + scroll_.view_h) scroll_.go_to(ui_, bottom - scroll_.view_h);
    scrolled();
  }
  return true;
}

std::string ChatPanel::link_of(int id) const {
  const auto number = static_cast<std::size_t>(id - LinkBase) / links_per_entry;
  const auto link = static_cast<std::size_t>(id - LinkBase) % links_per_entry;
  if (number < dropped_ || number - dropped_ >= log_.size()) return {};
  const std::wstring text = widen(log_[number - dropped_].text);
  const std::vector<LinkSpan> links = find_links(text);
  if (link >= links.size()) return {};
  return narrow(text.substr(links[link].start, links[link].length));
}

std::vector<MenuItem> ChatPanel::context_menu(int id, float x, float y) {
  menu_link_.clear();
  menu_for_input_ = id == Input || (id == -1 && ui_.focus == &input_);
  if (id == Input) {
    ui_.set_focus(this, &input_);
    input_.right_press(input_position(x, y));
  }
  if (menu_for_input_) return ui_.field_menu(input_);
  if (id >= LinkBase) {
    menu_link_ = link_of(id);
  } else if (id != Text && id != -1) {
    return {};
  }
  if (id != -1) {
    // Outside what is selected, the selection goes, as in a browser.
    const TextPoint at = log_point(x, y);
    if (log_selection_.empty() || at < log_selection_.start() || log_selection_.end() < at) log_selection_.clear();
    ui_.selection_owner = this;
  }
  std::vector<MenuItem> items;
  if (!menu_link_.empty()) items.push_back({MenuCommand::CopyLink, true});
  items.push_back({MenuCommand::Copy, !log_selection_.empty()});
  items.push_back({MenuCommand::SelectAll, !log_.empty()});
  return items;
}

void ChatPanel::menu_command(MenuCommand command) {
  if (menu_for_input_) {
    ui_.set_focus(this, &input_);
    ui_.field_command(input_, command);
  } else if (command == MenuCommand::CopyLink) {
    ui_.copy(widen(menu_link_));
  } else if (command == MenuCommand::Copy) {
    if (!log_selection_.empty()) ui_.copy(selected_text());
  } else if (command == MenuCommand::SelectAll) {
    select_all_messages();
  }
}

void ChatPanel::select_all_messages() {
  if (log_.empty()) return;
  ui_.set_focus(nullptr, nullptr);
  ui_.selection_owner = this;
  log_selection_.select({dropped_, 0}, {dropped_ + log_.size() - 1, widen(log_.back().text).size()});
}

void ChatPanel::release() { selecting_ = selecting_log_ = false; }

std::wstring_view ChatPanel::text_of(std::size_t number) {
  if (number < dropped_ || number - dropped_ >= log_.size()) return {};
  block_text_ = widen(log_[number - dropped_].text);
  return block_text_;
}

std::wstring ChatPanel::selected_text() {
  return log_selection_.text([this](std::size_t n) { return text_of(n); });
}

TextPoint ChatPanel::log_point(float x, float y) {
  const std::size_t base = dropped_ + first_;
  if (rows_.empty()) return {base, 0};
  const LogGeometry& g = log_geometry_;
  const float lx = (x - g.x) / g.scale, ly = (y - g.y) / g.scale;
  // The last row whose text starts above the point; above them all, the start of the first.
  std::size_t r = rows_.size();
  while (r > 0 && rows_[r - 1].y_text > ly) --r;
  if (r == 0) return {base, 0};
  --r;
  const Row& row = rows_[r];
  const std::wstring text = widen(log_[first_ + r].text);
  if (ly >= row.y_text + row.h_text) return {base + r, text.size()};
  ComPtr<IDWriteTextLayout> spare;
  IDWriteTextLayout* layout = row.text.get();
  if (!layout) {
    spare = ui_.gfx.layout(text, g.style, g.width, 100000);
    layout = spare.get();
  }
  if (!layout) return {base + r, 0};
  BOOL trailing = FALSE, inside = FALSE;
  DWRITE_HIT_TEST_METRICS m{};
  layout->HitTestPoint(lx, ly - row.y_text, &trailing, &inside, &m);
  return {base + r, std::min<std::size_t>(m.textPosition + (trailing ? m.length : 0), text.size())};
}

void ChatPanel::scroll_while_selecting() {
  if (!selecting_log_) return;
  const Rect& view = log_geometry_.view;
  const float past = drag_y_ < view.y ? drag_y_ - view.y : drag_y_ > view.bottom() ? drag_y_ - view.bottom() : 0;
  if (past == 0) {
    scrolled_at_ = 0;
    return;
  }
  // Pixels a second: a little past the edge creeps, far past it runs. Frames come only while held there.
  const float speed = std::clamp(std::abs(past) * 10, 60.0f, 2400.0f);
  const unsigned long long now = ui_.now_ms;
  const float elapsed = scrolled_at_ == 0 ? 16.0f : static_cast<float>(std::min<unsigned long long>(now - scrolled_at_, 50));
  scrolled_at_ = now;
  scroll_.jump_to(scroll_.offset + (past < 0 ? -speed : speed) * elapsed / 1000);
  scroll_.clamp();
  scrolled();
  ui_.more_frames = true;
}

bool ChatPanel::wheel(float x, float y, float delta) {
  if (!area_.contains(x, y)) return false;
  if (ui_.moves()) scroll_.wheel(ui_, delta);
  else scroll_.wheel(ui_, delta, 48);
  scrolled();
  return true;
}

void ChatPanel::scrolled() {
  stick_ = scroll_.target >= scroll_.max_offset() - 4;
  // Without moving what is on screen.
  if (scroll_.target < 60 && first_ > 0) {
    first_ = first_ > chat_page ? first_ - chat_page : 0;
    dirty_ = true;
    older_pending_ = true;
  }
}

void ChatPanel::drag(float x, float y) {
  if (selecting_log_) {
    drag_x_ = x;
    drag_y_ = y;
    log_selection_.drag_to(log_point(x, y), [this](std::size_t n) { return text_of(n); });
    return;
  }
  if (selecting_) {
    input_.drag_to(input_position(x, y));
    return;
  }
  const float travel = std::max(1.0f, bar_track_.h - scroll_.thumb(bar_track_).h);
  scroll_.jump_to(grab_offset_ + (y - grab_y_) * scroll_.max_offset() / travel);
  scrolled();
}

void ChatPanel::enter(bool shift) {
  if (shift) input_.insert(std::wstring_view(L"\n", 1));
  else send();
}

bool ChatPanel::key(unsigned vk, bool ctrl, bool shift) {
  if (ui_.focus != &input_) {  // the messages have the keys
    if (ctrl && !shift && (vk == 'C' || vk == VK_INSERT)) {
      if (!log_selection_.empty()) ui_.copy(selected_text());
      return true;
    }
    if (ctrl && !shift && vk == 'A' && !log_.empty()) {
      select_all_messages();
      return true;
    }
    return false;
  }
  if (vk == VK_UP || vk == VK_DOWN) {
    const int dir = vk == VK_UP ? -1 : 1;
    // Past the first or last line, the arrows go through what was sent.
    if (!input_.move_vertical(ui_.gfx, pack_input_style_, input_text_w_, dir, shift) && !shift && !ctrl && !input_.composing()) recall(dir);
    return true;
  }
  return ui_.edit_key(input_, vk, ctrl, shift);
}

}  // namespace crpp::ui
