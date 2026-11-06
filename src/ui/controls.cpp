#include "crpp/ui/controls.hpp"

namespace crpp::ui {

void paint_switch(Painter& p, const Theme& t, Rect r, bool on, bool disabled, bool hot) {
  const float a = disabled ? 0.5f : 1.0f;
  Color track = on ? t.hl : (t.dark ? t.hl_pale2 : rgb(0xcfe3ef));
  if (hot && !disabled) track = mix(rgb(0x000000), track, 5);
  p.fill_round(r, 14, with_alpha(track, a));
  const float knob = r.h - 6;
  const float tx = r.x + (on ? r.w - r.h + 3 : 3);
  p.fill_ellipse(tx + knob / 2, r.y + r.h / 2, knob / 2, knob / 2,
                 with_alpha(t.dark ? (on ? t.on_accent : t.hl_text_soft) : t.surface, a));
}

}  // namespace crpp::ui
