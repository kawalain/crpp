#include "crpp/ui/settings_panel.hpp"

#include <algorithm>

namespace crpp::ui {

namespace {

constexpr unsigned long long scrollbar_pause_ms = 2000;

constexpr std::array<const char* locale::Strings::*, 5> tab_buttons{&locale::Strings::tab_sound, &locale::Strings::tab_display, &locale::Strings::tab_notify,
                                                                    &locale::Strings::tab_system, &locale::Strings::tab_account};

}  // namespace

SettingsTab& SettingsPanel::tab() { return tab_of(page_); }

SettingsTab& SettingsPanel::tab_of(SettingsPage page) {
  switch (page) {
    case SettingsPage::Display:
      return display_;
    case SettingsPage::Notify:
      return notify_;
    case SettingsPage::System:
      return system_;
    case SettingsPage::Account:
      return account_;
    default:
      return sound_;
  }
}

void SettingsPanel::reset_scrollbar() {
  bar_shown_ = bar_over_ = reveal_pending_ = false;
  hide_at_ = 0;
}

void SettingsPanel::open(SettingsPage page) {
  open_ = true;
  ui_.set_focus(nullptr, nullptr);
  select(page);
}

void SettingsPanel::close() {
  open_ = false;
  ui_.set_focus(nullptr, nullptr);
  tab().escape();  // an open dropdown does not outlive the panel
}

void SettingsPanel::select(SettingsPage page) {
  if (open_ && page != page_ && ui_.animations) {
    left_page_ = page_;
    left_scroll_ = scroll_.drawn();
    left_fade_top_ = scroll_.fade_top();
    left_fade_bottom_ = scroll_.fade_bottom();
    left_at_ = clock_ms();
    ++tab_changes_;
  }
  ++selects_;
  pending_scroll_ = -1;
  page_ = page;
  scroll_.jump_to(0);
  reset_scrollbar();
  tab().shown();
}

void SettingsPanel::scroll_to(float offset) {
  // Possibly before the first frame, which would otherwise find nothing to scroll.
  scroll_.view_h = content_area_.h;
  scroll_.content_h = tab().content_height();
  scroll_.jump_to(offset);
  if (scroll_.content_h <= 0) pending_scroll_ = offset;
}

void SettingsPanel::paint(Painter& p) {
  if (const skin::Screen* screen = ui_.pack_screen("settings")) paint_pack(p, *screen);
}

void SettingsPanel::watch_scrollbar() {
  if (scroll_.max_offset() <= 0) {  // .no-overflow
    reset_scrollbar();
    return;
  }
  // .settings-scrollbar: the pointer on it, seen or not, brings it up and keeps it.
  const bool over = ui_.hot(this, Track) || ui_.hot(this, Thumb) || ui_.capture == this;
  if (over) {
    bar_shown_ = true;
    hide_at_ = 0;
  } else if (bar_over_ || reveal_pending_) {
    hide_at_ = ui_.now_ms + scrollbar_pause_ms;
  }
  bar_over_ = over;
  reveal_pending_ = false;
}

void SettingsPanel::before_click(const Surface* owner, int id) {
  if (open_) tab().before_click(owner, id);
}

// A track that jumps to the pointer has nothing to repeat.
bool SettingsPanel::repeats(int id) const { return id == BarUp || id == BarDown || (id == Track && !ui_.scales_stage()); }

void SettingsPanel::click(int id, float, float y, bool) {
  if (id == Droplet) {
    if (open_) close();
    else open(page_);
  } else if (id >= TabBase && id < TabBase + static_cast<int>(tab_buttons.size())) {
    select(static_cast<SettingsPage>(id - TabBase));
  } else if (id == Thumb) {
    ui_.capture = this;
    grab_y_ = ui_.to_stage_y(y);
    grab_offset_ = scroll_.offset;
  } else if (repeats(id)) {
    scroll_.step_towards(scroll_track_, y);
  } else if (id == Track) {
    scroll_.drag_thumb(scroll_track_, ui_.to_stage_y(y));
    bar_shown_ = true;
    reveal_pending_ = true;
  } else if (id == Version) {
    if (on_version) on_version();
  }
}

void SettingsPanel::drag(float, float y) {
  const float travel = std::max(1.0f, scroll_track_.h - scroll_.thumb(scroll_track_).h);
  scroll_.jump_to(grab_offset_ + (ui_.to_stage_y(y) - grab_y_) * scroll_.max_offset() / travel);
}

bool SettingsPanel::wheel(float x, float y, float delta) {
  if (!open_) return false;
  if (!tab().wheel(x, y, delta)) {
    const float before = scroll_.target;
    scroll_.wheel(ui_, delta);
    if (scroll_.target != before) {
      bar_shown_ = true;
      reveal_pending_ = true;
    }
  }
  return true;  // nothing under the panel scrolls
}

bool SettingsPanel::escape() {
  if (!open_) return false;
  if (!tab().escape()) close();
  return true;
}

Cursor SettingsPanel::cursor(int id) const {
  if (id >= TabBase || id == Droplet || id == Thumb || id == Version) return Cursor::Hand;
  return Cursor::Arrow;
}

bool SettingsPanel::animating() const { return open_ && tab().animating(); }

bool SettingsPanel::tick(unsigned long long now_ms) {
  bool changed = false;
  if (hide_at_ != 0 && now_ms >= hide_at_) {
    hide_at_ = 0;
    bar_shown_ = false;
    changed = true;
  }
  if (open_ && tab().tick(now_ms)) changed = true;
  const bool running = animating();
  changed = changed || running || was_animating_;  // the last frame shows the end state
  was_animating_ = running;
  return changed;
}

unsigned long long SettingsPanel::wake_at() const {
  if (!open_) return 0;
  const unsigned long long own = hide_at_, other = tab().wake_at();
  if (own == 0) return other;
  return other == 0 ? own : std::min(own, other);
}

}  // namespace crpp::ui
