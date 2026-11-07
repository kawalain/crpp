// The Luna Skin: re-created from public values as vector shapes; nothing here comes from a
// Windows theme file. Lines are drawn as rectangles on whole DIPs, so they stay crisp.
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

// Three corners' worth of a rounded rectangle's line, in single pixels.
void rounded_line(Painter& p, Rect r, Color c) {
  p.fill_rect({r.x + 2, r.y, r.w - 4, 1}, c);
  p.fill_rect({r.x + 2, r.bottom() - 1, r.w - 4, 1}, c);
  p.fill_rect({r.x, r.y + 2, 1, r.h - 4}, c);
  p.fill_rect({r.right() - 1, r.y + 2, 1, r.h - 4}, c);
  p.fill_rect({r.x + 1, r.y + 1, 1, 1}, c);
  p.fill_rect({r.right() - 2, r.y + 1, 1, 1}, c);
  p.fill_rect({r.x + 1, r.bottom() - 2, 1, 1}, c);
  p.fill_rect({r.right() - 2, r.bottom() - 2, 1, 1}, c);
}

void triangle_down(Painter& p, float cx, float cy, Color c) {
  for (int row = 0; row < 4; ++row) {
    const float half = static_cast<float>(3 - row);
    p.fill_rect({cx - half, cy - 2 + static_cast<float>(row), 2 * half + 1, 1}, c);
  }
}

void triangle_up(Painter& p, float cx, float cy, Color c) {
  for (int row = 0; row < 4; ++row) {
    const float half = static_cast<float>(row);
    p.fill_rect({cx - half, cy - 2 + static_cast<float>(row), 2 * half + 1, 1}, c);
  }
}

}  // namespace

void button(Painter& p, const Theme& t, Rect r, std::string_view label, const ButtonState& s) {
  const LunaColors& l = t.luna;
  const Rect inner{r.x + 1, r.y + 1, r.w - 2, r.h - 2};
  if (s.pressed && !s.off) p.fill_down(inner, l.button_down);
  else p.fill_down(inner, l.button);
  rounded_line(p, r, s.off ? l.edge_dark : l.button_line);
  if (!s.off && !s.pressed) {
    if (s.hot) {
      p.fill_rect({inner.x, inner.y, inner.w, 1}, l.hover_0);
      p.fill_rect({inner.x, inner.y + 1, inner.w, 1}, l.hover_1);
      p.fill_rect({inner.x, inner.y + 2, 1, inner.h - 3}, l.hover_2);
      p.fill_rect({inner.right() - 1, inner.y + 2, 1, inner.h - 3}, l.hover_2);
      p.fill_rect({inner.x, inner.bottom() - 1, inner.w, 1}, l.hover_3);
    } else if (s.primary || s.focused) {
      p.fill_rect({inner.x, inner.y, inner.w, 1}, l.focus_0);
      p.fill_rect({inner.x, inner.y + 1, inner.w, 1}, l.focus_2);
      p.fill_rect({inner.x, inner.y + 2, 1, inner.h - 3}, l.focus_1);
      p.fill_rect({inner.right() - 1, inner.y + 2, 1, inner.h - 3}, l.focus_1);
      p.fill_rect({inner.x, inner.bottom() - 1, inner.w, 1}, l.focus_3);
    }
  }
  const TextStyle style = body(p, r.h, Align::Center);
  if (s.off) {
    p.text(label, {r.x + 1, r.y + 1, r.w, r.h}, style, l.off_shadow);
    p.text(label, r, style, l.off_ink);
  } else {
    const float shift = s.pressed ? 1.0f : 0.0f;
    p.text(label, {r.x + shift, r.y + shift, r.w, r.h}, style, l.ink);
  }
}

float button_width(const Painter& p, std::string_view label) {
  DWRITE_TEXT_METRICS m{};
  if (const auto layout = p.gfx().layout(widen(label), body(p, button_h), 1000)) layout->GetMetrics(&m);
  return std::max(button_w, std::ceil(m.widthIncludingTrailingWhitespace) + 24);
}

void toolbar_button(Painter& p, const Theme& t, Rect r, std::string_view label, bool hot, bool pressed, bool lit) {
  const LunaColors& l = t.luna;
  if (hot || pressed || lit) {
    if (pressed || lit) p.fill_rect(r, mix(l.face, l.edge_dark, 60));
    p.fill_rect({r.x, r.y, r.w, 1}, pressed || lit ? l.edge_dark : l.edge_light);
    p.fill_rect({r.x, r.y, 1, r.h}, pressed || lit ? l.edge_dark : l.edge_light);
    p.fill_rect({r.x, r.bottom() - 1, r.w, 1}, pressed || lit ? l.edge_light : l.edge_dark);
    p.fill_rect({r.right() - 1, r.y, 1, r.h}, pressed || lit ? l.edge_light : l.edge_dark);
  }
  const float shift = pressed ? 1.0f : 0.0f;
  p.text(label, {r.x + 24 + shift, r.y + shift, r.w - 28, r.h}, body(p, r.h), l.ink);
}

void field(Painter& p, const Theme& t, Rect r, bool off) {
  p.fill_rect(r, off ? t.luna.field_off : t.luna.field);
  p.frame(r, t.luna.field_line);
}

void sunken(Painter& p, const Theme& t, Rect r) { field(p, t, r); }

void check_box(Painter& p, const Theme& t, Rect b, bool on, bool hot, bool off) {
  const LunaColors& l = t.luna;
  const Rect inner{b.x + 1, b.y + 1, b.w - 2, b.h - 2};
  p.fill_rect(inner, off ? l.field_off : l.check_to);
  if (!off) {
    // The face runs from the upper left, in a few steps as old themes do.
    for (int i = 0; i < 3; ++i) {
      const auto n = static_cast<float>(i);
      const Color shade = mix(l.check_from, l.check_to, 60 - 20 * n);
      p.fill_rect({inner.x + n, inner.y + n, inner.w - n, 1}, shade);
      p.fill_rect({inner.x + n, inner.y + n, 1, inner.h - n}, shade);
    }
  }
  p.frame(b, off ? l.edge_dark : l.check_line);
  if (hot && !off) {
    p.frame(inner, l.check_hot_0);
    p.frame({inner.x + 1, inner.y + 1, inner.w - 2, inner.h - 2}, l.check_hot_1);
  }
  if (on) {
    const Color tick = off ? l.off_ink : l.tick;
    constexpr float ys[7] = {5, 6, 7, 6, 5, 4, 3};
    for (int i = 0; i < 7; ++i) p.fill_rect({b.x + 3 + static_cast<float>(i), b.y + ys[i], 1, 3}, tick);
  }
}

void combo_arrow(Painter& p, const Theme& t, Rect r, bool hot, bool pressed) {
  const LunaColors& l = t.luna;
  p.fill_rect(r, pressed ? l.bar_thumb_low : hot ? mix(l.bar_thumb, l.edge_light, 50) : l.bar_thumb);
  rounded_line(p, r, l.edge_light);
  triangle_down(p, r.x + std::floor(r.w / 2), r.y + std::floor(r.h / 2), l.bar_arrow);
}

void picture_frame(Painter& p, const Theme& t, Rect r, float glow) {
  const LunaColors& l = t.luna;
  if (glow > 0) {
    p.frame({r.x - 3, r.y - 3, r.w + 6, r.h + 6}, with_alpha(l.glow_2, glow));
    p.frame({r.x - 2, r.y - 2, r.w + 4, r.h + 4}, with_alpha(l.glow_1, glow));
    p.frame({r.x - 1, r.y - 1, r.w + 2, r.h + 2}, with_alpha(l.glow_0, glow));
  }
  p.fill_rect(r, l.field);
  p.frame(r, l.picture_line);
}

void group_box(Painter& p, const Theme& t, Rect r, std::string_view legend) {
  const LunaColors& l = t.luna;
  rounded_line(p, r, l.group_line);
  DWRITE_TEXT_METRICS m{};
  const TextStyle style = body(p, 16);
  if (const auto layout = p.gfx().layout(widen(legend), style, r.w - 16)) layout->GetMetrics(&m);
  const float w = std::ceil(m.widthIncludingTrailingWhitespace) + 6;
  p.fill_rect({r.x + 8, r.y - 1, w, 3}, l.face);
  p.text(legend, {r.x + 11, r.y - 8, w, 16}, style, l.legend);
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
  const auto arrow_button = [&](Rect r, bool hot) {
    p.fill_rect({r.x + 1, r.y + 1, r.w - 2, r.h - 2}, hot ? mix(l.bar_thumb, l.edge_light, 50) : l.bar_thumb);
    rounded_line(p, r, l.edge_light);
  };
  arrow_button(parts.up, up_hot);
  triangle_up(p, parts.up.x + std::floor(parts.up.w / 2), parts.up.y + std::floor(parts.up.h / 2), l.bar_arrow);
  arrow_button(parts.down, down_hot);
  triangle_down(p, parts.down.x + std::floor(parts.down.w / 2), parts.down.y + std::floor(parts.down.h / 2), l.bar_arrow);
  if (parts.track.h >= 16 && parts.thumb.h < parts.track.h) {
    const Rect thumb = parts.thumb;
    p.fill_rect({thumb.x + 1, thumb.y + 1, thumb.w - 2, thumb.h - 2}, thumb_hot ? mix(l.bar_thumb, l.edge_light, 50) : l.bar_thumb);
    p.fill_rect({thumb.right() - 2, thumb.y + 1, 1, thumb.h - 2}, l.bar_thumb_in);
    p.fill_rect({thumb.x + 1, thumb.bottom() - 2, thumb.w - 2, 1}, l.bar_thumb_low);
    rounded_line(p, thumb, l.edge_light);
    // The grip.
    const float mid = std::floor(thumb.y + thumb.h / 2);
    for (float y = mid - 3; y <= mid + 3; y += 2) {
      p.fill_rect({thumb.x + 5, y, thumb.w - 10, 1}, l.edge_light);
      p.fill_rect({thumb.x + 5, y + 1, thumb.w - 10, 1}, l.bar_thumb_low);
    }
  }
}

Rect dialog_bounds(Rect r) { return {r.x - frame_w, r.y - 22, r.w + 2 * frame_w, r.h + 22 + frame_w}; }

void dialog_frame(Painter& p, const Theme& t, Rect r, std::string_view title) {
  const LunaColors& l = t.luna;
  const Rect bounds = dialog_bounds(r);
  p.fill_rect(bounds, l.frame_in);
  p.fill_rect({bounds.x, bounds.y, bounds.w, 1}, l.frame_out);
  p.fill_rect({bounds.x, bounds.y, 1, bounds.h}, l.frame_out);
  p.fill_rect({bounds.right() - 1, bounds.y, 1, bounds.h}, l.frame_low_out);
  p.fill_rect({bounds.x, bounds.bottom() - 1, bounds.w, 1}, l.frame_low_out);
  const Rect caption{bounds.x + 1, bounds.y + 1, bounds.w - 2, 21};
  p.fill_down(caption, l.caption);
  caption_text(p, t, {caption.x + 5, caption.y, caption.w - 10, caption.h}, title);
  p.fill_rect(r, l.face);
}

void window_frame(Painter& p, const Theme& t, float width, float height, bool active) {
  const LunaColors& l = t.luna;
  p.fill_down({0, 0, width, caption_h}, active ? l.caption : l.caption_idle);
  const Color idle = mix(l.frame_in, l.edge_light, 70);
  const auto ring = [&](float inset, Color left, Color right, Color bottom) {
    p.fill_rect({inset, caption_h, 1, height - caption_h - inset}, active ? left : idle);
    p.fill_rect({width - 1 - inset, caption_h, 1, height - caption_h - inset}, active ? right : idle);
    p.fill_rect({inset, height - 1 - inset, width - 2 * inset, 1}, active ? bottom : idle);
  };
  ring(0, l.frame_out, l.frame_low_out, l.frame_low_out);
  ring(1, l.frame_mid, l.frame_low_mid, l.frame_low_mid);
  ring(2, l.frame_in, l.frame_low_in, l.frame_low_in);
}

void caption_text(Painter& p, const Theme& t, Rect r, std::string_view text) {
  const TextStyle style{.size = 13, .weight = 700, .line_height = r.h, .ellipsis = true, .exact = true, .role = FontRole::Caption};
  p.text(text, {r.x + 1, r.y + 1, r.w, r.h}, style, t.luna.caption_shadow);
  p.text(text, r, style, t.luna.caption_ink);
}

void caption_button(Painter& p, const Theme& t, Rect r, CaptionGlyph glyph, bool hot, bool pressed) {
  const LunaColors& l = t.luna;
  const bool close = glyph == CaptionGlyph::Close;
  if (pressed) p.fill_rect(r, close ? rgb(0xb8351a) : rgb(0x1740a8));
  else if (close) p.fill_down(r, hot ? l.close_hot : l.close);
  else p.fill_down(r, hot ? l.caption_button_hot : l.caption_button);
  rounded_line(p, r, l.caption_ink);
  const Color ink = l.caption_ink;
  if (close) {
    const float inset = r.w >= 21 ? 6 : 5;
    for (float n = 0; n < r.w - 2 * inset; ++n) {
      p.fill_rect({r.x + inset + n, r.y + inset + n, 2, 1}, ink);
      p.fill_rect({r.right() - inset - 2 - n, r.y + inset + n, 2, 1}, ink);
    }
  } else {
    p.fill_rect({r.x + 5, r.bottom() - 7, 8, 3}, ink);
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
        luna::dialog_frame(p, t, r, "Cloudroom++");
        break;
      case PanelKind::List:
        p.fill_rect(r, l.field);
        p.frame(r, l.ink);
        break;
      case PanelKind::Menu:
        p.fill_rect({r.x + 2, r.y + 2, r.w, r.h}, rgb(0x000000, 0.25f));
        p.fill_rect(r, l.menu);
        p.frame(r, l.menu_line);
        break;
      case PanelKind::Popup:
      case PanelKind::Picker:
        p.fill_rect(r, l.face);
        p.frame(r, l.edge_out);
        p.fill_rect({r.x + 1, r.y + 1, r.w - 2, 1}, l.edge_light);
        p.fill_rect({r.x + 1, r.y + 1, 1, r.h - 2}, l.edge_light);
        p.fill_rect({r.x + 1, r.bottom() - 2, r.w - 2, 1}, l.edge_dark);
        p.fill_rect({r.right() - 2, r.y + 1, 1, r.h - 2}, l.edge_dark);
        break;
      case PanelKind::Update:
      case PanelKind::Notice:
      case PanelKind::Toast:
        p.fill_down(r, l.info);
        p.frame(r, l.info_line);
        break;
    }
  }

  [[nodiscard]] Rect panel_bounds(Rect r, PanelKind kind) const override { return kind == PanelKind::Modal ? luna::dialog_bounds(r) : r; }

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
    const float cy = std::floor(r.y + r.h / 2);
    p.fill_rect({r.x, cy - 1, r.w, 1}, l.track_edge);
    p.fill_rect({r.x, cy, r.w, 1}, l.track);
    p.fill_rect({r.x, cy + 1, r.w, 1}, l.edge_light);
    const float pos = std::floor(r.x + slider_thumb / 2 + (r.w - slider_thumb) * at);
    const Rect thumb{pos - 5, cy - 9, 11, 19};
    p.fill_down({thumb.x + 1, thumb.y + 1, thumb.w - 2, thumb.h - 2}, l.thumb);
    p.frame(thumb, l.thumb_line);
    p.fill_rect({thumb.x + 1, thumb.y + 1, thumb.w - 2, 2}, l.tick);
    p.fill_rect({thumb.x + 1, thumb.bottom() - 3, thumb.w - 2, 2}, l.tick);
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
    p.fill_rect(thumb, t.luna.bar_thumb);
    p.frame(thumb, t.luna.edge_light);
  }

  [[nodiscard]] Color bubble(Painter& p, const Theme& t, Rect r, float, Color, Color, float) const override {
    p.fill_rect(r, t.luna.tooltip);
    p.frame(r, t.luna.tooltip_line);
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
      constexpr float ys[7] = {2, 3, 4, 3, 2, 1, 0};
      const float top = std::floor(r.y + (r.h - 7) / 2);
      for (int i = 0; i < 7; ++i) p.fill_rect({r.x + 5 + static_cast<float>(i), top + ys[i], 1, 3}, ink);
    }
  }

  void avatar(Painter& p, const Theme& t, Rect r, float, bool lit, float) const override {
    luna::picture_frame(p, t, r, lit ? 1.0f : 0.0f);
  }

  void progress(Painter& p, const Theme& t, Rect track, int percent) const override {
    const LunaColors& l = t.luna;
    const Rect bar{track.x, std::floor(track.y + track.h / 2 - 7), track.w, 14};
    p.fill_rect(bar, l.field);
    p.frame(bar, l.progress_line);
    const float inner = bar.w - 4;
    const float filled = inner * static_cast<float>(percent) / 100;
    for (float x = 0; x + 8 <= filled + 8 && x < inner; x += 10) {
      const float w = std::min(8.0f, inner - x);
      p.fill_down({bar.x + 2 + x, bar.y + 2, w, bar.h - 4}, l.progress);
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
