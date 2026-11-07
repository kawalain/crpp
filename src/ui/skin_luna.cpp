// The Luna Skin: re-created from public values as vector shapes; nothing here comes from a
// Windows theme file.
#include <algorithm>
#include <cmath>

#include "crpp/ui/luna.hpp"
#include "crpp/ui/skin.hpp"

namespace crpp::ui {

namespace luna {

TextStyle body(const Gfx& gfx, float line_height, Align align) {
  return {.size = gfx.classic_size(), .line_height = line_height, .align = align, .ellipsis = true, .exact = true};
}

TextStyle bold(const Painter& p, float line_height, Align align) {
  TextStyle style = body(p, line_height, align);
  style.weight = 700;
  return style;
}

TextStyle paragraph(const Painter& p, Align align) {
  const float size = p.gfx().classic_size();
  return {.size = size, .line_height = size + 4, .align = align, .wrap = true, .exact = true, .keep_words = true};
}

namespace {

constexpr float small_radius = 2, button_radius = 3;

// A box with a rounded line around it, on whole pixels. What it returns is where it went.
Rect round_box(Painter& p, Rect r, float radius, Color fill, Color line) {
  const Rect o = p.snap(r);
  p.fill_round(o, radius, fill);
  p.stroke_round(o, radius, line, p.line());
  return o;
}

// A small triangle, smoothed, pointing down or up around this point.
void triangle(Painter& p, float cx, float cy, bool down, Color c) {
  ID2D1PathGeometry* const shape = p.gfx().path(down ? "M-4 -2H4L0 2.5Z" : "M-4 2H4L0 -2.5Z");
  if (!shape) return;
  const Rect at = p.snap({cx, cy, 1, 1});
  p.push(D2D1::Matrix3x2F::Translation(at.x, at.y));
  p.fill_geometry(shape, c);
  p.pop();
}

}  // namespace

void button(Painter& p, const Theme& t, Rect r, std::string_view label, const ButtonState& s) {
  const LunaColors& l = t.luna;
  const Rect o = p.snap(r);
  const float u = p.line();
  const Rect inner{o.x + u, o.y + u, o.w - 2 * u, o.h - 2 * u};
  if (s.pressed && !s.off) p.fill_round_down(o, button_radius, l.button_down);
  else p.fill_round_down(o, button_radius, l.button);
  if (!s.off && !s.pressed && (s.hot || s.primary || s.focused)) {
    const Color top = s.hot ? l.hover_0 : l.focus_0, next = s.hot ? l.hover_1 : l.focus_2, side = s.hot ? l.hover_2 : l.focus_1, low = s.hot ? l.hover_3 : l.focus_3;
    p.fill_rect({inner.x, inner.y, inner.w, u}, top);
    p.fill_rect({inner.x, inner.y + u, inner.w, u}, next);
    p.fill_rect({inner.x, inner.y + 2 * u, u, inner.h - 3 * u}, side);
    p.fill_rect({inner.right() - u, inner.y + 2 * u, u, inner.h - 3 * u}, side);
    p.fill_rect({inner.x, inner.bottom() - u, inner.w, u}, low);
  }
  p.stroke_round(o, button_radius, s.off ? l.edge_dark : l.button_line, u);
  const TextStyle style = body(p, o.h, Align::Center);
  if (s.off) {
    p.text(label, {o.x + u, o.y + u, o.w, o.h}, style, l.off_shadow);
    p.text(label, o, style, l.off_ink);
  } else {
    const float shift = s.pressed ? u : 0.0f;
    p.text(label, {o.x + shift, o.y + shift, o.w, o.h}, style, l.ink);
  }
}

float button_width(const Painter& p, std::string_view label) {
  DWRITE_TEXT_METRICS m{};
  if (const auto layout = p.gfx().layout(widen(label), body(p, button_h), 1000)) layout->GetMetrics(&m);
  return std::max(button_w, std::ceil(m.widthIncludingTrailingWhitespace) + 24);
}

void toolbar_button(Painter& p, const Theme& t, Rect r, std::string_view label, bool hot, bool pressed, bool lit) {
  const LunaColors& l = t.luna;
  const Rect o = p.snap(r);
  const float u = p.line();
  if (hot || pressed || lit) {
    if (pressed || lit) p.fill_rect(o, mix(l.face, l.edge_dark, 60));
    const Color first = pressed || lit ? l.edge_dark : l.edge_light, second = pressed || lit ? l.edge_light : l.edge_dark;
    p.fill_rect({o.x, o.y, o.w, u}, first);
    p.fill_rect({o.x, o.y, u, o.h}, first);
    p.fill_rect({o.x, o.bottom() - u, o.w, u}, second);
    p.fill_rect({o.right() - u, o.y, u, o.h}, second);
  }
  const float shift = pressed ? u : 0.0f;
  p.text(label, {o.x + 24 + shift, o.y + shift, o.w - 28, o.h}, body(p, o.h), l.ink);
}

void field(Painter& p, const Theme& t, Rect r, bool off) {
  p.fill_rect(r, off ? t.luna.field_off : t.luna.field);
  p.frame(r, t.luna.field_line);  // both land on the same pixels
}

void sunken(Painter& p, const Theme& t, Rect r) { field(p, t, r); }

void check_box(Painter& p, const Theme& t, Rect b, bool on, bool hot, bool off) {
  const LunaColors& l = t.luna;
  const Rect o = p.snap(b);
  const float u = p.line();
  const Rect inner{o.x + u, o.y + u, o.w - 2 * u, o.h - 2 * u};
  p.fill_rect(inner, off ? l.field_off : l.check_to);
  if (!off) {
    // The face runs from the upper left, in a few steps as old themes do.
    for (int i = 0; i < 3; ++i) {
      const float n = static_cast<float>(i) * u;
      const Color shade = mix(l.check_from, l.check_to, 60 - 20 * static_cast<float>(i));
      p.fill_rect({inner.x + n, inner.y + n, inner.w - n, u}, shade);
      p.fill_rect({inner.x + n, inner.y + n, u, inner.h - n}, shade);
    }
  }
  p.frame(o, off ? l.edge_dark : l.check_line);
  if (hot && !off) {
    p.frame(inner, l.check_hot_0);
    p.frame({inner.x + u, inner.y + u, inner.w - 2 * u, inner.h - 2 * u}, l.check_hot_1);
  }
  if (on) {
    p.push(D2D1::Matrix3x2F::Translation(o.x, o.y));
    if (ID2D1PathGeometry* const shape = p.gfx().path("M3.5 6.5 5.5 8.5 9.5 4")) p.stroke_geometry(shape, off ? l.off_ink : l.tick, 2, false);
    p.pop();
  }
}

void combo_arrow(Painter& p, const Theme& t, Rect r, bool hot, bool pressed) {
  const LunaColors& l = t.luna;
  const Rect o = round_box(p, r, small_radius, pressed ? l.bar_thumb_low : hot ? mix(l.bar_thumb, l.edge_light, 50) : l.bar_thumb, l.edge_light);
  triangle(p, o.x + o.w / 2, o.y + o.h / 2, true, l.bar_arrow);
}

void picture_frame(Painter& p, const Theme& t, Rect r, float glow) {
  const LunaColors& l = t.luna;
  const Rect o = p.snap(r);
  const float u = p.line();
  if (glow > 0) {
    p.frame({o.x - 3 * u, o.y - 3 * u, o.w + 6 * u, o.h + 6 * u}, with_alpha(l.glow_2, glow));
    p.frame({o.x - 2 * u, o.y - 2 * u, o.w + 4 * u, o.h + 4 * u}, with_alpha(l.glow_1, glow));
    p.frame({o.x - u, o.y - u, o.w + 2 * u, o.h + 2 * u}, with_alpha(l.glow_0, glow));
  }
  p.fill_rect(o, l.field);
  p.frame(o, l.picture_line);
}

void group_box(Painter& p, const Theme& t, Rect r, std::string_view legend) {
  const LunaColors& l = t.luna;
  const Rect o = p.snap(r);
  p.stroke_round(o, button_radius, l.group_line, p.line());
  DWRITE_TEXT_METRICS m{};
  const TextStyle style = body(p, 16);
  if (const auto layout = p.gfx().layout(widen(legend), style, r.w - 16)) layout->GetMetrics(&m);
  const float w = std::ceil(m.widthIncludingTrailingWhitespace) + 6;
  p.fill_rect({o.x + 8, o.y - 1, w, 3}, l.face);
  p.text(legend, {o.x + 11, o.y - 8, w, 16}, style, l.legend);
}

void link(Painter& p, const Theme& t, Rect r, std::string_view text, bool hot) {
  const TextStyle style = body(p, r.h);
  p.text(text, r, style, t.luna.link);
  if (hot) {
    DWRITE_TEXT_METRICS m{};
    if (const auto layout = p.gfx().layout(widen(text), style, r.w)) layout->GetMetrics(&m);
    p.fill_rect({r.x, std::floor(r.y + r.h / 2 + p.gfx().classic_size() / 2), m.width, 1}, t.luna.link);
  }
}

ScrollbarParts scrollbar_parts(Rect bar, float view_h, float content_h, float offset) {
  ScrollbarParts parts;
  parts.up = {bar.x, bar.y, bar.w, std::min(bar.w, bar.h / 2)};
  parts.down = {bar.x, bar.bottom() - parts.up.h, bar.w, parts.up.h};
  parts.track = {bar.x, parts.up.bottom(), bar.w, parts.down.y - parts.up.bottom()};
  const float room = content_h > view_h ? content_h - view_h : 0;
  const float h = content_h > 0 ? std::max(16.0f, std::floor(parts.track.h * std::min(1.0f, view_h / content_h))) : parts.track.h;
  const float y = room > 0 ? parts.track.y + std::floor((parts.track.h - h) * offset / room) : parts.track.y;
  parts.thumb = {bar.x, y, bar.w, h};
  return parts;
}

void scrollbar(Painter& p, const Theme& t, const ScrollbarParts& parts, bool thumb_hot, bool up_hot, bool down_hot) {
  const LunaColors& l = t.luna;
  p.fill_rect(parts.track, l.bar_track_from);
  const auto arrow_button = [&](Rect r, bool hot, bool down) {
    const Rect o = round_box(p, r, small_radius, hot ? mix(l.bar_thumb, l.edge_light, 50) : l.bar_thumb, l.edge_light);
    triangle(p, o.x + o.w / 2, o.y + o.h / 2, down, l.bar_arrow);
  };
  arrow_button(parts.up, up_hot, false);
  arrow_button(parts.down, down_hot, true);
  if (parts.track.h >= 16 && parts.thumb.h < parts.track.h) {
    const Rect thumb = round_box(p, parts.thumb, small_radius, thumb_hot ? mix(l.bar_thumb, l.edge_light, 50) : l.bar_thumb, l.edge_light);
    const float u = p.line();
    // The grip.
    const float mid = p.snap({thumb.x, thumb.y + thumb.h / 2, 1, 1}).y;
    for (float n = -3; n <= 3; n += 2) {
      p.fill_rect({thumb.x + 5, mid + n * u, thumb.w - 10, u}, l.edge_light);
      p.fill_rect({thumb.x + 5, mid + (n + 1) * u, thumb.w - 10, u}, l.bar_thumb_low);
    }
  }
}

void window_frame(Painter& p, const Theme& t, float width, float height, bool active) {
  const LunaColors& l = t.luna;
  const float u = p.line();
  const float cap = p.snap({0, 0, 1, caption_h}).h, band = p.snap({0, 0, frame_w, 1}).w;
  // The region keeps every pixel the curve touches. Those it only partly covers are blended
  // with this, since a window cannot blend with what is behind it.
  const Color matte = mix(active ? l.frame_out : l.edge_dark, rgb(0x808080), 50);
  p.fill_rect({0, 0, window_radius + 1, window_radius + 1}, matte);
  p.fill_rect({width - window_radius - 1, 0, window_radius + 2, window_radius + 1}, matte);
  p.rt()->PushAxisAlignedClip(D2D1::RectF(0, 0, width, cap), D2D1_ANTIALIAS_MODE_ALIASED);
  p.fill_round_gradient({0, 0, width, cap + 2 * window_radius}, window_radius, {0, 0}, {0, cap}, active ? l.caption : l.caption_idle);
  p.rt()->PopAxisAlignedClip();
  const Color idle = mix(l.frame_in, l.edge_light, 70);
  const auto ring = [&](float inset, float w, Color left, Color right, Color bottom) {
    p.fill_rect({inset, cap, w, height - cap - inset}, active ? left : idle);
    p.fill_rect({width - inset - w, cap, w, height - cap - inset}, active ? right : idle);
    p.fill_rect({inset, height - inset - w, width - 2 * inset, w}, active ? bottom : idle);
  };
  ring(2 * u, band - 2 * u, l.frame_in, l.frame_low_in, l.frame_low_in);
  ring(u, u, l.frame_mid, l.frame_low_mid, l.frame_low_mid);
  ring(0, u, l.frame_out, l.frame_low_out, l.frame_low_out);
}

void caption_text(Painter& p, const Theme& t, Rect r, std::string_view text) {
  const TextStyle style{.size = 13, .weight = 700, .line_height = r.h, .ellipsis = true, .exact = true, .role = FontRole::Caption};
  p.text(text, {r.x + 1, r.y + 1, r.w, r.h}, style, t.luna.caption_shadow);
  p.text(text, r, style, t.luna.caption_ink);
}

void caption_button(Painter& p, const Theme& t, Rect r, CaptionGlyph glyph, bool hot, bool pressed) {
  const LunaColors& l = t.luna;
  const bool close = glyph == CaptionGlyph::Close;
  const Rect o = p.snap(r);
  if (pressed) p.fill_round(o, button_radius, close ? rgb(0xb8351a) : rgb(0x1740a8));
  else if (close) p.fill_round_down(o, button_radius, hot ? l.close_hot : l.close);
  else p.fill_round_down(o, button_radius, hot ? l.caption_button_hot : l.caption_button);
  p.stroke_round(o, button_radius, l.caption_ink, p.line());
  const Color ink = l.caption_ink;
  if (close) {
    const float inset = o.w * 0.3f;
    p.stroke_line(o.x + inset, o.y + inset, o.right() - inset, o.bottom() - inset, ink, 2);
    p.stroke_line(o.right() - inset, o.y + inset, o.x + inset, o.bottom() - inset, ink, 2);
  } else {
    p.fill_rect({o.x + o.w * 0.25f, o.bottom() - o.h * 0.34f, o.w * 0.38f, 3}, ink);
  }
}

}  // namespace luna

namespace {

class LunaSkin final : public Skin {
 public:
  [[nodiscard]] bool scales_stage() const override { return false; }
  [[nodiscard]] bool moves() const override { return false; }

  void panel(Painter& p, const Theme& t, Rect r, PanelKind kind, float) const override {
    const LunaColors& l = t.luna;
    switch (kind) {
      case PanelKind::Modal:
        p.fill_rect(r, l.face);
        break;
      case PanelKind::List:
        p.fill_rect(r, l.field);
        p.frame(r, t.dark ? l.menu_line : l.ink);
        break;
      case PanelKind::Menu:
        p.fill_rect({r.x + 2, r.y + 2, r.w, r.h}, rgb(0x000000, 0.25f));
        p.fill_rect(r, l.menu);
        p.frame(r, l.menu_line);
        break;
      case PanelKind::Popup:
      case PanelKind::Picker: {
        const Rect o = p.snap(r);
        const float u = p.line();
        p.fill_rect(o, l.face);
        p.frame(o, l.edge_out);
        p.fill_rect({o.x + u, o.y + u, o.w - 2 * u, u}, l.edge_light);
        p.fill_rect({o.x + u, o.y + u, u, o.h - 2 * u}, l.edge_light);
        p.fill_rect({o.x + u, o.bottom() - 2 * u, o.w - 2 * u, u}, l.edge_dark);
        p.fill_rect({o.right() - 2 * u, o.y + u, u, o.h - 2 * u}, l.edge_dark);
        break;
      }
      case PanelKind::Update:
      case PanelKind::Notice:
      case PanelKind::Toast:
        p.fill_down(r, l.info);
        p.frame(r, l.info_line);
        break;
    }
  }

  [[nodiscard]] DialogMetrics dialog_metrics() const override { return {14, 14, 12, 14, luna::button_h, luna::button_w, 6, 280, 340, 16, 18, 16, Align::Left}; }

  void backdrop(Painter&, const Theme&, Rect, float) const override {}  // a dialog of the time dims nothing

  void button(Painter& p, const Theme& t, Rect r, std::string_view label, const ButtonLook& b) const override {
    luna::button(p, t, r, label, {.hot = b.hot, .pressed = b.pressed, .primary = b.primary});
  }

  void switch_at(Painter& p, const Theme& t, Rect r, bool on, float, bool disabled, bool hot) const override {
    luna::check_box(p, t, {r.x, std::floor(r.y + (r.h - luna::check_size) / 2), luna::check_size, luna::check_size}, on, hot, disabled);
  }

  void slider(Painter& p, const Theme& t, Rect r, float at) const override {
    const LunaColors& l = t.luna;
    const float u = p.line();
    const float cy = p.snap({r.x, r.y + r.h / 2, 1, 1}).y;
    p.fill_rect({r.x, cy - u, r.w, u}, l.track_edge);
    p.fill_rect({r.x, cy, r.w, u}, l.track);
    p.fill_rect({r.x, cy + u, r.w, u}, l.edge_light);
    const float pos = r.x + slider_thumb / 2 + (r.w - slider_thumb) * at;
    const Rect thumb = p.snap({pos - 5, cy - 9, 11, 19});
    p.fill_round_down(thumb, luna::small_radius, l.thumb);
    p.fill_rect({thumb.x + u, thumb.y + u, thumb.w - 2 * u, 2 * u}, l.tick);
    p.fill_rect({thumb.x + u, thumb.bottom() - 3 * u, thumb.w - 2 * u, 2 * u}, l.tick);
    p.stroke_round(thumb, luna::small_radius, l.thumb_line, u);
  }

  void dropdown_button(Painter& p, const Theme& t, Rect r, std::string_view text, bool open, bool hot, bool disabled) const override {
    luna::field(p, t, r, disabled);
    p.text(text, {r.x + 4, r.y, r.w - 24, r.h}, luna::body(p, r.h), disabled ? t.luna.off_ink : t.luna.ink);
    luna::combo_arrow(p, t, {r.right() - 17, r.y + 2, 15, r.h - 4}, hot, open);
  }

  [[nodiscard]] ListMetrics list_metrics() const override { return {1, 17, 0, 17 * 8 + 2, 0}; }

  void list_item(Painter& p, const Theme& t, Rect r, std::string_view text, bool selected, bool hot) const override {
    const bool lit = selected || hot;
    if (lit) p.fill_rect(r, t.luna.select);
    p.text(text, {r.x + 3, r.y, r.w - 6, r.h}, luna::body(p, r.h), lit ? t.luna.select_ink : t.luna.ink);
  }

  void scrollbar(Painter& p, const Theme& t, Rect thumb, float, float) const override {
    (void)luna::round_box(p, thumb, luna::small_radius, t.luna.bar_thumb, t.luna.edge_light);
  }

  [[nodiscard]] Color bubble(Painter& p, const Theme& t, Rect r, float, Color, Color, float) const override {
    const Rect o = p.snap(r);
    p.fill_rect(o, t.luna.tooltip);
    p.frame(o, t.luna.tooltip_line);
    return t.luna.tooltip_ink;
  }

  void cell(Painter& p, const Theme& t, Rect r, bool selected, bool hot) const override {
    if (selected) p.fill_rect(r, t.hl_soft);
    if (selected || hot) p.frame(r, t.luna.select);
  }

  void menu_item(Painter& p, const Theme& t, Rect r, std::string_view text, bool active, bool hot, bool disabled) const override {
    const LunaColors& l = t.luna;
    if (hot) p.fill_rect(r, l.select);
    const Color ink = disabled ? l.off_ink : hot ? l.select_ink : l.ink;
    p.text(text, {r.x + 20, r.y, r.w - 24, r.h}, luna::body(p, r.h), ink);
    if (active) {
      p.push(D2D1::Matrix3x2F::Translation(r.x + 2, std::round(r.y + (r.h - 13) / 2)));
      if (ID2D1PathGeometry* const shape = p.gfx().path("M3.5 6.5 5.5 8.5 9.5 4")) p.stroke_geometry(shape, ink, 2, false);
      p.pop();
    }
  }

  void avatar(Painter& p, const Theme& t, Rect r, float, bool lit, float) const override {
    luna::picture_frame(p, t, r, lit ? 1.0f : 0.0f);
  }

  void progress(Painter& p, const Theme& t, Rect track, int percent) const override {
    const LunaColors& l = t.luna;
    const Rect bar = p.snap({track.x, track.y + track.h / 2 - 7, track.w, 14});
    p.fill_rect(bar, l.field);
    p.frame(bar, l.progress_line);
    const float inner = bar.w - 4;
    const float filled = inner * static_cast<float>(percent) / 100;
    for (float x = 0; x + 8 <= filled + 8 && x < inner; x += 10) {
      const float w = std::min(8.0f, inner - x);
      const Rect block = p.snap({bar.x + 2 + x, bar.y + 2, w, bar.h - 4});
      p.fill_down(block, l.progress);
    }
  }

  void close_button(Painter& p, const Theme& t, Rect r, bool hot, float) const override {
    luna::caption_button(p, t, r, luna::CaptionGlyph::Close, hot, false);
  }
};

}  // namespace

const Skin& luna_skin() {
  static const LunaSkin skin;
  return skin;
}

}  // namespace crpp::ui
