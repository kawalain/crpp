#include "crpp/ui/settings_panel.hpp"

namespace crpp::ui {

namespace {

constexpr const char* droplet_path = "M18 1C13 10 2 20 2 31a16 16 0 0 0 32 0C34 20 23 10 18 1Z";
constexpr const char* droplet_x_path = "M12 25 24 37M24 25 12 37";

constexpr Rect content_area{296, 137, 566, 504};  // .settings-content
constexpr Rect scroll_track{835, 187, 8, 403};    // .settings-scrollbar
constexpr float fade_h = 27;

struct TabButton {
  const char* name;
  float y;
};
constexpr std::array<TabButton, 5> tab_buttons{
    {{"소리", 309.5f}, {"디스플레이", 355.5f}, {"알림", 401.5f}, {"시스템", 447.5f}, {"계정", 493.5f}}};

}  // namespace

bool SettingsPanel::init() {
  droplet_ = ui_.gfx.parse_path(droplet_path);
  droplet_x_ = ui_.gfx.parse_path(droplet_x_path);
  return droplet_ && droplet_x_;
}

SettingsTab& SettingsPanel::tab() {
  switch (page_) {
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

void SettingsPanel::open(SettingsPage page) {
  open_ = true;
  ui_.set_focus(nullptr, nullptr);
  select(page);
}

void SettingsPanel::close() {
  open_ = false;
  ui_.set_focus(nullptr, nullptr);
}

void SettingsPanel::select(SettingsPage page) {
  page_ = page;
  scroll_.offset = 0;
  tab().shown();
}

void SettingsPanel::paint(Painter& p) {
  if (open_) paint_panel(p);
  paint_droplet(p);
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
  p.fill_geometry(ui_.cloud.get(), cloud);
  p.pop();

  for (std::size_t i = 0; i < tab_buttons.size(); ++i) {
    const int id = TabBase + static_cast<int>(i);
    const Rect r{103, tab_buttons[i].y, 172, 37};
    const bool active = static_cast<std::size_t>(page_) == i;
    const bool hot = ui_.hot(this, id);
    if (hot) p.push(scale_about(r.x + r.w / 2, r.y + r.h / 2, 1.04f));
    if (active) p.fill_round(r, 9, t.hl);
    else if (hot) p.fill_round(r, 9, t.hl_soft);
    p.text(tab_buttons[i].name, {r.x + 10, r.y, r.w - 20, r.h}, {.size = 20, .weight = active ? 700 : 400, .line_height = r.h},
           active || hot ? t.on_accent : t.hl_text_soft);
    if (hot) p.pop();
    ui_.add_hit(this, r, id);
  }

  SettingsTab& current = tab();
  scroll_.view_h = content_area.h;
  scroll_.content_h = current.content_height();
  scroll_.clamp();
  const Rect area{content_area.x, content_area.y - scroll_.offset, 550, scroll_.content_h};
  ui_.push_hit_clip(ui_.to_window(content_area));
  p.rt()->PushAxisAlignedClip(D2D1::RectF(content_area.x, content_area.y, content_area.right(), content_area.bottom()),
                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  current.paint(p, area);
  // Content fades where it runs under the edges (.settings-content mask). The panel
  // is the only thing behind it.
  const auto fade = [&](float y0, float y1, float from, float to) {
    ComPtr<ID2D1RectangleGeometry> g;
    if (FAILED(ui_.gfx.d2d()->CreateRectangleGeometry(D2D1::RectF(content_area.x, std::min(y0, y1), content_area.right(), std::max(y0, y1)),
                                                      g.put()))) {
      return;
    }
    p.fill_vertical_gradient(g.get(), y0, y1, {{0, with_alpha(cloud, from)}, {1, with_alpha(cloud, to)}});
  };
  if (scroll_.offset > 0) fade(content_area.y, content_area.y + fade_h, 1, 0);
  if (scroll_.offset < scroll_.max_offset()) fade(content_area.bottom() - fade_h, content_area.bottom(), 0, 1);
  p.rt()->PopAxisAlignedClip();
  ui_.pop_hit_clip();

  if (scroll_.max_offset() > 0) {
    paint_scrollbar(p, t, scroll_, scroll_track, ui_.hot(this, Thumb) || ui_.capture == this);
    ui_.add_hit(this, scroll_.thumb(scroll_track), Thumb);
  }
  current.paint_popups(p, area);

  if (!version_.empty()) {
    const Rect r{103, 624, 172, 20};
    p.text(version_, r, {.size = 14, .line_height = 20, .align = Align::Right},
           ui_.hot(this, Version) ? t.hl_text : rgb(0x8a94a0));
    ui_.add_hit(this, r, Version);
  }
}

void SettingsPanel::paint_droplet(Painter& p) {
  // Fixed to the window's top-right corner, not the stage.
  const Theme& t = ui_.theme;
  p.set_base(D2D1::Matrix3x2F::Identity());
  const Rect drop{ui_.width - 18 - 35, 16, 35, 49};
  const bool hot = ui_.hot(this, Droplet);
  const float s = 35 / 36.f;
  D2D1_MATRIX_3X2_F m = D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(drop.x, drop.y + (49 - 49 * s) / 2);
  if (hot) m = m * scale_about(drop.x + 17.5f, drop.y + 24.5f, 1.08f);
  p.push(m);
  p.fill_geometry(droplet_.get(), hot ? mix(t.droplet, t.hl_text_dark, 92) : t.droplet);
  if (open_) p.stroke_geometry(droplet_x_.get(), hot ? t.hl_text_dark : t.hl_text, 4.4f, false);
  p.pop();
  ui_.add_hit_window(this, drop, Droplet);
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
    scroll_.drag_thumb(scroll_track, ui_.to_stage_y(y));
  } else if (id == Version) {
    if (on_version) on_version();
  }
}

void SettingsPanel::drag(float, float y) { scroll_.drag_thumb(scroll_track, ui_.to_stage_y(y)); }

bool SettingsPanel::wheel(float x, float y, float delta) {
  if (!open_) return false;
  if (!tab().wheel(x, y, delta)) scroll_.wheel(delta);
  return true;  // nothing under the panel scrolls
}

bool SettingsPanel::escape() {
  if (!open_) return false;
  if (!tab().escape()) close();
  return true;
}

bool SettingsPanel::animating() const { return open_ && const_cast<SettingsPanel*>(this)->tab().animating(); }

Cursor SettingsPanel::cursor(int id) const { return id == Overlay ? Cursor::Arrow : Cursor::Hand; }

}  // namespace crpp::ui
