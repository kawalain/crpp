#include "crpp/ui/settings_panel.hpp"

#include <algorithm>

namespace crpp::ui {

namespace {

constexpr const char* droplet_path = "M18 1C13 10 2 20 2 31a16 16 0 0 0 32 0C34 20 23 10 18 1Z";
constexpr const char* droplet_x_path = "M12 25 24 37M24 25 12 37";

constexpr Rect content_area = settings_view;
constexpr Rect scroll_track{835, 187, 8, 403};  // .settings-scrollbar
constexpr unsigned long long scrollbar_pause_ms = 2000;

struct TabButton {
  const char* locale::Strings::*name;
  float y;
};
constexpr std::array<TabButton, 5> tab_buttons{
    {{&locale::Strings::tab_sound, 309.5f},
     {&locale::Strings::tab_display, 355.5f},
     {&locale::Strings::tab_notify, 401.5f},
     {&locale::Strings::tab_system, 447.5f},
     {&locale::Strings::tab_account, 493.5f}}};

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
  bar_ = Fade{};
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
  }
  page_ = page;
  scroll_.jump_to(0);
  reset_scrollbar();
  tab().shown();
}

void SettingsPanel::scroll_to(float offset) {
  // Possibly before the first frame, which would otherwise find nothing to scroll.
  scroll_.view_h = content_area.h;
  scroll_.content_h = tab().content_height();
  scroll_.jump_to(offset);
}

void SettingsPanel::paint(Painter& p) {
  panel_.set(ui_, open_, modal_pop_ms);
  const float shown = panel_.value(ui_);
  if (shown > 0) {
    const bool moving = shown < 1;
    if (moving) {
      ui_.more_frames = true;
      p.push_opacity(shown);
      p.set_moved(0, 6 * (1 - shown));
    }
    ui_.hits_enabled = open_;  // closing, it is only a picture
    paint_panel(p);
    ui_.hits_enabled = true;
    if (moving) {
      p.set_moved(0, 0);
      p.pop_opacity();
    }
  }
  paint_droplet(p);
}

void SettingsPanel::paint_tabs(Painter& p) {
  const Theme& t = ui_.theme;
  for (std::size_t i = 0; i < tab_buttons.size(); ++i) {
    const int id = TabBase + static_cast<int>(i);
    const Rect r{103, tab_buttons[i].y, 172, 37};
    const bool active = static_cast<std::size_t>(page_) == i;
    TabAnim& anim = tab_anim_[i];
    anim.active.set(ui_, active, 150);
    anim.hover.set(ui_, ui_.hot(this, id), 150);
    anim.grow.set(ui_, ui_.hot(this, id), 140);
    const float a = anim.active.value(ui_), h = anim.hover.value(ui_), g = anim.grow.value(ui_);
    const float lit = std::max(a, h);
    if (g > 0) p.push(scale_about(r.x + r.w / 2, r.y + r.h / 2, 1 + 0.04f * g));
    if (lit > 0) p.fill_round(r, 9, with_alpha(mix(t.hl, t.hl_soft, a * 100), lit));
    p.text(tr().*tab_buttons[i].name, {r.x + 10, r.y, r.w - 20, r.h}, {.size = 20, .weight = active ? 700 : 400, .line_height = r.h},
           mix(t.on_accent, t.hl_text_soft, lit * 100));
    if (g > 0) p.pop();
    ui_.add_hit(this, r, id);
  }
}

void SettingsPanel::paint_panel(Painter& p) {
  const Theme& t = ui_.theme;
  p.set_base(D2D1::Matrix3x2F::Identity());
  const Rect window{0, 0, ui_.width, ui_.height};
  p.fill_rect(window, rgb(0x000000, 0.255f));
  ui_.add_hit_window(this, window, Overlay);
  p.set_base(D2D1::Matrix3x2F::Scale(ui_.scale, ui_.scale) * D2D1::Matrix3x2F::Translation(ui_.ox, ui_.oy));

  // The cloud, mirrored (viewBox 0 0 496 330 stretched to 1091x733 at 15,-35).
  p.push(D2D1::Matrix3x2F::Translation(-496, 0) * D2D1::Matrix3x2F::Scale(-1091.0f / 496, 733.0f / 330) *
         D2D1::Matrix3x2F::Translation(15, -35));
  const Color cloud = t.dark ? t.surface : rgb(0xF7FCFF, 0.98f);
  p.fill_geometry(ui_.cloud(), cloud);
  p.pop();

  paint_tabs(p);

  SettingsTab& current = tab();
  scroll_.view_h = content_area.h;
  scroll_.content_h = current.content_height();
  scroll_.clamp();
  scroll_.advance(ui_);
  const Rect area{content_area.x, content_area.y - scroll_.drawn(), 550, scroll_.content_h};
  ui_.push_hit_clip(ui_.to_window(content_area));
  p.rt()->PushAxisAlignedClip(D2D1::RectF(content_area.x, content_area.y, content_area.right(), content_area.bottom()),
                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  // Each tab has its own scroll, so each keeps the edge fades of where it stands.
  const auto paint_tab = [&](SettingsTab& shown_tab, Rect at, float top, float bottom) {
    const bool fade = push_edge_fade(ui_, p, content_area, top, bottom);
    shown_tab.paint(p, at);
    pop_edge_fade(p, fade);
  };
  constexpr float tab_fade_ms = 140;
  const float change = left_at_ != 0 ? transition(ui_, left_at_, tab_fade_ms) : 1;
  if (change < 1) {
    ui_.more_frames = true;
    SettingsTab& left = tab_of(left_page_);
    const bool hits = ui_.hits_enabled;
    ui_.hits_enabled = false;
    p.push_opacity(1 - change, content_area);
    paint_tab(left, {content_area.x, content_area.y - left_scroll_, 550, left.content_height()}, left_fade_top_, left_fade_bottom_);
    p.pop_opacity();
    ui_.hits_enabled = hits;
    p.push_opacity(change, content_area);
    paint_tab(current, area, scroll_.fade_top(), scroll_.fade_bottom());
    p.pop_opacity();
  } else {
    left_at_ = 0;
    paint_tab(current, area, scroll_.fade_top(), scroll_.fade_bottom());
  }
  p.rt()->PopAxisAlignedClip();
  ui_.pop_hit_clip();

  paint_scrollbar_faded(p);
  current.paint_popups(p, area);
  paint_version(p);
}

void SettingsPanel::paint_scrollbar_faded(Painter& p) {
  if (scroll_.max_offset() <= 0) {  // .no-overflow
    reset_scrollbar();
    return;
  }
  const Theme& t = ui_.theme;
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

  bar_.set(ui_, bar_shown_, 150, 500);
  thumb_hover_.set(ui_, over, 120);
  const float shown = bar_.value(ui_);
  const Rect thumb = scroll_.thumb(scroll_track);
  if (shown > 0) ui_.skin().scrollbar(p, t, thumb, thumb_hover_.value(ui_), shown);
  ui_.add_hit(this, scroll_track, Track);
  ui_.add_hit(this, thumb, Thumb);
}

void SettingsPanel::paint_version(Painter& p) {
  if (version_.empty()) return;
  const Theme& t = ui_.theme;
  const Rect r{103, 624, 172, 20};
  p.text(version_, r, {.size = 14, .line_height = 20, .align = Align::Right},
         ui_.hot(this, Version) ? t.hl_text : rgb(0x8a94a0));
  ui_.add_hit(this, r, Version);
  version_tip_.set(ui_, ui_.hot(this, Version), 150);
  // .version .tip: over the button's right edge, 15 px, rising 4 px as it appears.
  if (const float shown = version_tip_.value(ui_); shown > 0) {
    const char* tip = tr().tip_changelog;
    const TextStyle style{.size = 15, .weight = 700, .line_height = 22};
    const float w = text_width(p, tip, style) + 30;
    paint_bubble(p, t, tip, {r.right() - w, r.y - 8 - 32 + 4 * (1 - shown), w, 32}, style, 11, t.hl, t.on_accent, shown);
  }
}

void SettingsPanel::paint_droplet(Painter& p) {
  // Fixed to the window's top-right corner, not the stage.
  const Theme& t = ui_.theme;
  p.set_base(D2D1::Matrix3x2F::Identity());
  const Rect drop{ui_.width - 18 - 35, 16, 35, 49};
  const bool hot = ui_.hot(this, Droplet);
  droplet_hover_.set(ui_, hot, 150);
  droplet_grow_.set(ui_, hot, 140);
  const float h = droplet_hover_.value(ui_), g = droplet_grow_.value(ui_);
  const float s = 35 / 36.f;
  D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(drop.x, drop.y + (49 - 49 * s) / 2);
  if (g > 0) m = m * scale_about(drop.x + 17.5f, drop.y + 24.5f, 1 + 0.08f * g);
  p.push(m);
  p.fill_geometry(ui_.gfx.path(droplet_path), mix(mix(t.droplet, t.hl_text_dark, 92), t.droplet, h * 100));
  if (open_) p.stroke_geometry(ui_.gfx.path(droplet_x_path), mix(t.hl_text_dark, t.hl_text, h * 100), 4.4f, false);
  p.pop();
  ui_.add_hit_window(this, drop, Droplet);

  // .droplet .tip: only while the panel is closed, under the droplet, falling 4 px, and it
  // scales with the droplet.
  droplet_tip_.set(ui_, hot && !open_, 150);
  if (const float shown = droplet_tip_.value(ui_); shown > 0 && !open_) {
    const char* tip = tr().tip_settings;
    const TextStyle style{.size = 16, .weight = 700, .line_height = 22};
    const float w = text_width(p, tip, style) + 30;
    const Rect box{drop.right() - w, drop.bottom() + 8 - 4 * (1 - shown), w, 32};
    p.push(scale_about(drop.x + 17.5f, drop.y + 24.5f, 1 + 0.08f * g));
    p.shadow_round(box, 11, 8, 2, rgb(0x2e648c, 0.2f * shown));
    paint_bubble(p, t, tip, box, style, 11, t.surface, t.hl_text, shown);
    p.pop();
  }
}

void SettingsPanel::before_click(const Surface* owner, int id) {
  if (open_) tab().before_click(owner, id);
}

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
  } else if (id == Track) {
    scroll_.drag_thumb(scroll_track, ui_.to_stage_y(y));
    bar_shown_ = true;
    reveal_pending_ = true;
  } else if (id == Version) {
    if (on_version) on_version();
  }
}

void SettingsPanel::drag(float, float y) {
  const float travel = std::max(1.0f, scroll_track.h - scroll_.thumb(scroll_track).h);
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

bool SettingsPanel::animating() const {
  if (panel_.running(ui_)) return true;
  if (droplet_hover_.running(ui_) || droplet_grow_.running(ui_) || droplet_tip_.running(ui_)) return true;
  if (!open_) return false;
  if (bar_.running(ui_) || thumb_hover_.running(ui_) || version_tip_.running(ui_)) return true;
  for (const TabAnim& a : tab_anim_) {
    if (a.active.running(ui_) || a.hover.running(ui_) || a.grow.running(ui_)) return true;
  }
  return tab().animating();
}

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
