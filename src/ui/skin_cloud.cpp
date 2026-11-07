// The Cloud Skin: upstream's look, its values from style.css.
#include "crpp/ui/skin.hpp"

#include <algorithm>

#include "crpp/ui/surface.hpp"

namespace crpp::ui {

namespace {

constexpr const char* close_path = "M12 12 24 24M24 12 12 24";

class CloudSkin final : public Skin {
 public:
  [[nodiscard]] bool scales_stage() const override { return true; }
  [[nodiscard]] bool moves() const override { return true; }

  void panel(Painter& p, const Theme& t, Rect r, PanelKind kind, float shown) const override {
    // Some keep the light Theme's shadow in the dark one, as upstream's CSS has them.
    const bool themed = kind == PanelKind::Modal || kind == PanelKind::List || kind == PanelKind::Update;
    if (themed && t.dark) p.shadow_round(r, 14, 38, 12, rgb(0x000000, 0.33f * shown));
    else p.shadow_round(r, 14, 16, 4, rgb(0x2e78aa, 0.22f * shown));
    p.fill_round(r, 14, with_alpha(t.surface, shown));
  }

  void backdrop(Painter& p, const Theme&, Rect window, float shown) const override {
    // .modal-backdrop's fade is a little shorter than the card's rise.
    constexpr float fade_ms = 140, pop_ms = 160;
    p.fill_rect(window, rgb(0x365669, 0.34f * std::min(1.0f, shown * pop_ms / fade_ms)));
  }

  void button(Painter& p, const Theme& t, Rect r, std::string_view label, const ButtonLook& b) const override {
    struct Shape {
      float radius, grow, size;
      int weight;
    };
    const Shape shape = [&]() -> Shape {
      switch (b.kind) {
        case ButtonKind::Modal: return {10, 1.04f, 16, 700};    // .modal-confirm, .modal-cancel
        case ButtonKind::Notice: return {r.h / 2, 1, 15, 700};
        case ButtonKind::Update: return {9, 1.03f, 15, 700};    // .update-button
        case ButtonKind::Toast: return {8, 1, 15, 700};
        case ButtonKind::Settings: return {8, 1.03f, 16, 400};  // .open-logs, .reset-settings
        case ButtonKind::Account: return {8, 1.03f, 17, 400};   // .account-buttons
      }
      return {10, 1, 16, 400};
    }();
    const float at = b.hover_at >= 0 ? b.hover_at : (b.hot ? 1.0f : 0.0f);
    const bool grows = shape.grow != 1 && at > 0;
    if (grows) p.push(scale_about(r.x + r.w / 2, r.y + r.h / 2, at >= 1 ? shape.grow : 1 + (shape.grow - 1) * at));
    const TextStyle text{.size = shape.size, .weight = shape.weight, .line_height = r.h, .align = Align::Center};
    if (b.kind == ButtonKind::Settings) {
      p.fill_round(r, shape.radius, mix(t.hl_dark, t.hl, at * 100));
      p.text(label, r, text, t.on_accent);
    } else if (b.kind == ButtonKind::Notice && !b.primary) {
      if (b.hot) p.fill_round(r, shape.radius, t.hl_pale);
      p.text(label, r, text, t.ink_strong);
    } else {
      const Color fill = b.primary ? (b.hot ? t.hl_dark : t.hl) : (b.hot ? t.hl_soft : t.hl_pale2);
      const Color ink = b.primary || b.hot ? t.on_accent : t.hl_text;
      p.fill_round(r, shape.radius, with_alpha(fill, b.shown));
      p.text(label, r, text, with_alpha(ink, b.shown));
    }
    if (grows) p.pop();
  }

  void switch_at(Painter& p, const Theme& t, Rect r, bool on, float at, bool disabled, bool hot) const override {
    const float a = disabled ? 0.5f : 1.0f;
    Color track = on ? t.hl : (t.dark ? t.hl_pale2 : rgb(0xcfe3ef));
    if (hot && !disabled) track = mix(rgb(0x000000), track, 5);
    p.fill_round(r, 14, with_alpha(track, a));
    const float knob = r.h - 6;
    const float tx = r.x + 3 + (r.w - r.h) * at;
    p.fill_ellipse(tx + knob / 2, r.y + r.h / 2, knob / 2, knob / 2, with_alpha(t.dark ? (on ? t.on_accent : t.hl_text_soft) : t.surface, a));
  }

  void slider(Painter& p, const Theme& t, Rect r, float at) const override {
    const float cy = r.y + r.h / 2;
    const float pos = r.x + slider_thumb / 2 + (r.w - slider_thumb) * at;
    p.fill_round({r.x, cy - 3, r.w, 6}, 3, t.hl_pale2);
    p.fill_round({r.x, cy - 3, pos - r.x, 6}, 3, t.hl);
    p.fill_ellipse(pos, cy, slider_thumb / 2, slider_thumb / 2, t.hl);
    p.fill_ellipse(pos, cy, slider_thumb / 2 - 2, slider_thumb / 2 - 2, t.dark ? t.hl_text_dark : t.surface);
  }

  void dropdown_button(Painter& p, const Theme& t, Rect r, std::string_view text, bool open, bool hot, bool disabled) const override {
    const float a = disabled ? 0.5f : 1.0f;
    const Color fill = open ? t.hl_soft : hot ? t.hl_dark : t.hl;
    p.fill_round(r, 10, with_alpha(fill, a));
    p.text(text, {r.x + 12, r.y, r.w - 48, r.h}, {.size = 17, .weight = 700, .line_height = r.h, .ellipsis = true}, with_alpha(t.on_accent, a));
    const float cx = r.right() - 13 - 7, cy = r.y + r.h / 2;
    const Color chevron = rgb(0xFFFFFF, a);
    p.stroke_line(cx - 6, cy - 3, cx, cy + 3, chevron, 2);
    p.stroke_line(cx, cy + 3, cx + 6, cy - 3, chevron, 2);
  }

  [[nodiscard]] ListMetrics list_metrics() const override { return {6, 38, 4, 224, 6}; }  // li: padding 9 12, 20 tall, 4 apart

  void list_item(Painter& p, const Theme& t, Rect r, std::string_view text, bool selected, bool hot) const override {
    if (selected) p.fill_round(r, 9, t.hl);
    else if (hot) p.fill_round(r, 9, t.hl_soft);
    p.text(text, {r.x + 12, r.y + 9, r.w - 24, 20}, {.size = 16, .weight = selected ? 700 : 400, .line_height = 20, .ellipsis = true},
           selected || hot ? t.on_accent : t.hl_text);
  }

  void scrollbar(Painter& p, const Theme& t, Rect thumb, float hover, float shown) const override {
    p.fill_round(thumb, 5, with_alpha(mix(t.hl_soft, t.hl_pale2, hover * 100), shown));
  }

  [[nodiscard]] Color bubble(Painter& p, const Theme&, Rect r, float radius, Color fill, Color ink, float alpha) const override {
    p.fill_round(r, radius, with_alpha(fill, fill.a * alpha));
    return with_alpha(ink, ink.a * alpha);
  }

  void cell(Painter& p, const Theme& t, Rect r, bool selected, bool hot) const override {
    if (selected) p.fill_round(r, 10, t.hl_soft);
    else if (hot) p.fill_round(r, 10, t.hl_pale2);
  }

  void menu_item(Painter& p, const Theme& t, Rect r, std::string_view text, bool active, bool hot, bool disabled) const override {
    const Color fill = active ? (hot ? t.hl_dark : t.hl) : t.hl_soft;
    if (active || hot) p.fill_round(r, 9, fill);
    const Color ink = active || hot ? t.on_accent : t.hl_text;
    p.text(text, {r.x + 12, r.y + 8, r.w - 24, 22}, {.size = 17, .weight = 700, .line_height = 22}, with_alpha(ink, disabled ? 0.45f : 1.0f));
  }

  void avatar(Painter& p, const Theme& t, Rect r, float grow, bool lit, float shown) const override {
    p.fill_ellipse(r.x + r.w / 2, r.y + r.h / 2, r.w / 2 * grow, r.h / 2 * grow, with_alpha(lit ? t.hl_pale2 : t.hl_pale, shown));
  }

  void progress(Painter& p, const Theme& t, Rect track, int percent) const override {
    // The accent color is the sky's, so both parts are drawn in the surface color.
    p.fill_round(track, 4, with_alpha(t.surface, 0.35f));
    const float filled = std::max(track.h, track.w * static_cast<float>(percent) / 100);
    p.fill_round({track.x, track.y, filled, track.h}, 4, t.surface);
  }

  void close_button(Painter& p, const Theme& t, Rect r, bool hot, float shown) const override {
    if (hot) p.fill_ellipse(r.x + 18, r.y + 18, 18, 18, with_alpha(t.hl_pale2, shown));
    p.push(D2D1::Matrix3x2F::Translation(r.x, r.y));
    p.stroke_geometry(p.gfx().path(close_path), with_alpha(t.hl, shown), 4.4f, false);
    p.pop();
  }
};

}  // namespace

const Skin& cloud_skin() {
  static const CloudSkin skin;
  return skin;
}

}  // namespace crpp::ui
