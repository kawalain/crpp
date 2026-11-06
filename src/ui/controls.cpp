#include "crpp/ui/controls.hpp"

#include <algorithm>
#include <cmath>

namespace crpp::ui {

namespace {
constexpr float list_fade_ms = 120;
}

float ease(float t) {
  // cubic-bezier(.25, .1, .25, 1): find the curve's parameter for x = t, then its y.
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  const auto bezier = [](float a, float b, float u) {
    const float v = 1 - u;
    return 3 * v * v * u * a + 3 * v * u * u * b + u * u * u;
  };
  float low = 0, high = 1;
  for (int i = 0; i < 12; ++i) {
    const float u = (low + high) / 2;
    if (bezier(0.25f, 0.25f, u) < t) low = u;
    else high = u;
  }
  return bezier(0.1f, 1.0f, (low + high) / 2);
}

float transition(const Ui& ui, unsigned long long start, float duration_ms) {
  if (!ui.animations || ui.now_ms <= start) return ui.animations ? 0.0f : 1.0f;
  return ease(static_cast<float>(ui.now_ms - start) / duration_ms);
}

void paint_switch(Painter& p, const Theme& t, Rect r, bool on, bool disabled, bool hot) {
  paint_switch_at(p, t, r, on, on ? 1.0f : 0.0f, disabled, hot);
}

void paint_switch_at(Painter& p, const Theme& t, Rect r, bool on, float at, bool disabled, bool hot) {
  const float a = disabled ? 0.5f : 1.0f;
  Color track = on ? t.hl : (t.dark ? t.hl_pale2 : rgb(0xcfe3ef));
  if (hot && !disabled) track = mix(rgb(0x000000), track, 5);
  p.fill_round(r, 14, with_alpha(track, a));
  const float knob = r.h - 6;
  const float tx = r.x + 3 + (r.w - r.h) * at;
  p.fill_ellipse(tx + knob / 2, r.y + r.h / 2, knob / 2, knob / 2,
                 with_alpha(t.dark ? (on ? t.on_accent : t.hl_text_soft) : t.surface, a));
}

void paint_slider(Painter& p, const Theme& t, Rect r, int value, int min, int max) {
  constexpr float thumb = 21;
  const float cy = r.y + r.h / 2;
  const float at = static_cast<float>(value - min) / static_cast<float>(max - min);
  const float pos = r.x + thumb / 2 + (r.w - thumb) * at;
  p.fill_round({r.x, cy - 3, r.w, 6}, 3, t.hl_pale2);
  p.fill_round({r.x, cy - 3, pos - r.x, 6}, 3, t.hl);
  p.fill_ellipse(pos, cy, thumb / 2, thumb / 2, t.hl);
  p.fill_ellipse(pos, cy, thumb / 2 - 2, thumb / 2 - 2, t.dark ? t.hl_text_dark : t.surface);
}

int slider_value(Rect r, float x, int min, int max) {
  constexpr float thumb = 21;
  const float at = std::clamp((x - r.x - thumb / 2) / (r.w - thumb), 0.0f, 1.0f);
  return min + static_cast<int>(std::lround(at * static_cast<float>(max - min)));
}

void Dropdown::paint_button(Ui& ui, Surface* owner, Painter& p, Rect r) const {
  const Theme& t = ui.theme;
  const float a = disabled ? 0.5f : 1.0f;
  const bool hot = !disabled && ui.hot(owner, id_base);
  const Color fill = open ? t.hl_soft : hot ? t.hl_dark : t.hl;
  p.fill_round(r, 10, with_alpha(fill, a));
  if (selected >= 0 && selected < static_cast<int>(options.size())) {
    p.text(options[static_cast<std::size_t>(selected)], {r.x + 12, r.y, r.w - 48, r.h},
           {.size = 17, .weight = 700, .line_height = r.h, .ellipsis = true}, with_alpha(t.on_accent, a));
  }
  const float cx = r.right() - 13 - 7, cy = r.y + r.h / 2;
  const Color chevron = rgb(0xFFFFFF, a);
  p.stroke_line(cx - 6, cy - 3, cx, cy + 3, chevron, 2);
  p.stroke_line(cx, cy + 3, cx + 6, cy - 3, chevron, 2);
  ui.add_hit(owner, r, id_base);
}

void Dropdown::paint_list(Ui& ui, Surface* owner, Painter& p, Rect r) {
  if (!open) {
    was_open_ = false;
    return;
  }
  const Theme& t = ui.theme;
  constexpr float pad = 6, item_h = 38, gap = 4, max_h = 224;  // li: padding 9 12, 20 tall, 4 apart
  const float content = static_cast<float>(options.size()) * (item_h + gap) - gap + 2 * pad;
  const float h = std::min(content, max_h);
  list_ = {r.x, up ? r.y - 6 - h : r.bottom() + 6, r.w, h};
  scroll_.view_h = h;
  scroll_.content_h = content;
  scroll_.clamp();
  scroll_.advance(ui);
  if (!was_open_) {
    was_open_ = true;
    opened_at_ = ui.now_ms;
  }
  const float shown = transition(ui, opened_at_, list_fade_ms);
  if (shown < 1) {
    ui.more_frames = true;
    p.push_opacity(shown);
  }
  p.shadow_round(list_, 14, t.dark ? 38.0f : 16.0f, t.dark ? 12.0f : 4.0f, t.dark ? rgb(0x000000, 0.33f) : rgb(0x2e78aa, 0.22f));
  p.fill_round(list_, 14, t.surface);
  ui.push_hit_clip(ui.to_window(list_));
  p.rt()->PushAxisAlignedClip(D2D1::RectF(list_.x, list_.y, list_.right(), list_.bottom()),
                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  const bool fade = push_edge_fade(ui, p, list_, scroll_.fade_top(), scroll_.fade_bottom());
  float y = list_.y + pad - scroll_.drawn();
  for (std::size_t i = 0; i < options.size(); ++i, y += item_h + gap) {
    const int id = id_base + 1 + static_cast<int>(i);
    const Rect item{list_.x + pad, y, list_.w - 2 * pad, item_h};
    const bool is_selected = static_cast<int>(i) == selected;
    const bool hot = ui.hot(owner, id);
    if (is_selected) p.fill_round(item, 9, t.hl);
    else if (hot) p.fill_round(item, 9, t.hl_soft);
    p.text(options[i], {item.x + 12, item.y + 9, item.w - 24, 20},
           {.size = 16, .weight = is_selected ? 700 : 400, .line_height = 20, .ellipsis = true},
           is_selected || hot ? t.on_accent : t.hl_text);
    ui.add_hit(owner, item, id);
  }
  pop_edge_fade(p, fade);
  p.rt()->PopAxisAlignedClip();
  ui.pop_hit_clip();
  if (shown < 1) p.pop_opacity();
}

bool Dropdown::click(int id) {
  if (disabled) return false;
  if (id == id_base) {
    open = !open;
    scroll_ = {};
    return false;
  }
  const int index = id - id_base - 1;
  if (index < 0 || index >= static_cast<int>(options.size())) return false;
  open = false;
  if (index == selected) return false;
  selected = index;
  return true;
}

bool Dropdown::wheel(const Ui& ui, float x, float y, float delta) {
  if (!open || !list_.contains(ui.to_stage_x(x), ui.to_stage_y(y))) return false;
  scroll_.wheel(ui, delta, 40);
  return true;
}

void Scroll::clamp() {
  offset = std::clamp(offset, 0.0f, max_offset());
  target = std::clamp(target, 0.0f, max_offset());
}

void Scroll::jump_to(float at) {
  offset = target = std::clamp(at, 0.0f, max_offset());
  stretch = 0;
}

void Scroll::wheel(const Ui& ui, float delta, float px_per_notch) {
  const float aim = target - delta * px_per_notch;
  const float max = max_offset();
  if (!ui.animations) {
    jump_to(aim);
    return;
  }
  target = std::clamp(aim, 0.0f, max);
  const float past = aim - target;
  if (past != 0 && max > 0) {
    // The further out, the heavier the pull.
    constexpr float give = 0.4f;
    stretch = std::clamp(stretch + past * give * (1 - std::abs(stretch) / longest_stretch), -longest_stretch, longest_stretch);
    pulled_at = ui.now_ms;
  }
}

void Scroll::advance(Ui& ui) {
  if (!ui.animations) {
    jump_to(target);
    return;
  }
  if (!moving()) {
    moved_at = 0;
    return;
  }
  // Time constants for settling in about 160 ms from a notch away, and coming back from
  // the furthest stretch in about 240 ms. A longer gap between frames than the cap, as
  // when a dialog held them up, must not read as a jump.
  constexpr float settle_ms = 32, return_ms = 52, longest_step_ms = 50, near_px = 0.5f;
  constexpr unsigned long long hold_ms = 60;  // a wheel still turning keeps it stretched
  const float dt = moved_at == 0 ? 8 : std::min(longest_step_ms, static_cast<float>(ui.now_ms - std::min(ui.now_ms, moved_at)));
  moved_at = ui.now_ms;
  offset += (target - offset) * (1 - std::exp(-dt / settle_ms));
  if (std::abs(target - offset) < near_px) offset = target;
  if (stretch != 0 && ui.now_ms >= pulled_at + hold_ms) {
    stretch *= std::exp(-dt / return_ms);
    if (std::abs(stretch) < near_px) stretch = 0;
  }
  ui.more_frames = true;
}

float Scroll::fade_top() const { return std::clamp(drawn(), 0.0f, edge_fade_px); }

float Scroll::fade_bottom() const { return std::clamp(max_offset() - drawn(), 0.0f, edge_fade_px); }

Rect Scroll::thumb(Rect track) const {
  const float h = content_h > 0 ? std::max(28.0f, track.h * std::min(1.0f, view_h / content_h)) : track.h;
  const float max = max_offset();
  const float y = max > 0 ? track.y + (track.h - h) * offset / max : track.y;
  return {track.x, y, track.w, h};
}

void Scroll::drag_thumb(Rect track, float y) {
  const Rect t = thumb(track);
  const float room = track.h - t.h;
  if (room <= 0) return;
  jump_to((y - t.h / 2 - track.y) / room * max_offset());
}

void paint_scrollbar(Painter& p, const Theme& t, const Scroll& scroll, Rect track, bool hot) {
  if (scroll.max_offset() <= 0) return;
  p.fill_round(scroll.thumb(track), 5, hot ? t.hl_soft : t.hl_pale2);
}

Rect paint_modal(Ui& ui, Surface* owner, Painter& p, float w, float h, float shown, int backdrop_id, int card_id) {
  p.set_base(D2D1::Matrix3x2F::Identity());
  const Rect window{0, 0, ui.width, ui.height};
  // .modal-backdrop's fade is a little shorter than the card's rise.
  p.fill_rect(window, rgb(0x365669, 0.34f * std::min(1.0f, shown * modal_pop_ms / modal_fade_ms)));
  ui.add_hit_window(owner, window, backdrop_id);
  const Rect settled{(ui.width - w) / 2, (ui.height - h) / 2, w, h};
  const Rect card{settled.x, settled.y + 6 * (1 - shown), w, h};
  p.shadow_round(card, 14, ui.theme.dark ? 38.0f : 16.0f, ui.theme.dark ? 12.0f : 4.0f,
                 ui.theme.dark ? rgb(0x000000, 0.33f * shown) : rgb(0x2e78aa, 0.22f * shown));
  p.fill_round(card, 14, with_alpha(ui.theme.surface, shown));
  ui.add_hit_window(owner, settled, card_id);
  return card;
}

bool push_edge_fade(Ui& ui, Painter& p, Rect area, float top, float bottom) {
  if (top <= 0 && bottom <= 0) return false;
  ID2D1LinearGradientBrush* mask = ui.edge_fade_mask(p, area, top, bottom);
  if (!mask) return false;
  mask->SetStartPoint(D2D1::Point2F(0, area.y));
  mask->SetEndPoint(D2D1::Point2F(0, area.bottom()));
  p.rt()->PushLayer(D2D1::LayerParameters(D2D1::RectF(area.x, area.y, area.right(), area.bottom()), nullptr,
                                          D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 1.0f, mask),
                    nullptr);
  return true;
}

void paint_modal_button(Ui& ui, Surface* owner, Painter& p, Rect r, std::string_view label, bool primary, float shown, int id) {
  const Theme& t = ui.theme;
  const bool hot = ui.hot(owner, id);
  if (hot) p.push(scale_about(r.x + r.w / 2, r.y + r.h / 2, 1.04f));
  const Color fill = primary ? (hot ? t.hl_dark : t.hl) : (hot ? t.hl_soft : t.hl_pale2);
  const Color ink = primary || hot ? t.on_accent : t.hl_text;
  p.fill_round(r, 10, with_alpha(fill, shown));
  p.text(label, r, {.size = 16, .weight = 700, .line_height = r.h, .align = Align::Center}, with_alpha(ink, shown));
  if (hot) p.pop();
  ui.add_hit_window(owner, r, id);
}

}  // namespace crpp::ui
