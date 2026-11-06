#include "crpp/ui/settings_panel.hpp"

namespace crpp::ui {

namespace {
constexpr const char* droplet_path = "M18 1C13 10 2 20 2 31a16 16 0 0 0 32 0C34 20 23 10 18 1Z";
}  // namespace

bool SettingsPanel::init() {
  droplet_ = ui_.gfx.parse_path(droplet_path);
  return static_cast<bool>(droplet_);
}

void SettingsPanel::paint(Painter& p) {
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
  p.pop();
  ui_.add_hit_window(this, drop, Droplet);
}

void SettingsPanel::click(int id, float, float, bool) {
  if (id == Droplet && on_settings) on_settings();
}

}  // namespace crpp::ui
