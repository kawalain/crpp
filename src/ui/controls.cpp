#include "crpp/ui/controls.hpp"

#include <algorithm>
#include <cmath>

#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum ListNumberId : int { Items, Selected, Up, Offset, FadeTop, FadeBottom, Heading };
enum ListReportId : int { View, Content };
constexpr skin::Name list_text_names[] = {{"item", 0}};
constexpr skin::Name list_number_names[] = {{"items", Items}, {"selected", Selected}, {"up", Up}, {"scroll", Offset}, {"fade_top", FadeTop}, {"fade_bottom", FadeBottom}, {"heading", Heading}};
constexpr skin::Name list_hit_names[] = {{"item", 1}};
constexpr skin::Name list_report_names[] = {{"view", View}, {"content", Content}};
constexpr skin::Name list_note_names[] = {{"list", 0}};
}

skin::ScreenSchema Dropdown::schema() {
  return {.screen = "dropdown", .texts = list_text_names, .numbers = list_number_names, .hits = list_hit_names, .reports = list_report_names, .notes = list_note_names};
}

float ease(float t) { return skin::curve_at(skin::Curve::Ease, t); }

float transition(const Ui& ui, unsigned long long start, float duration_ms) {
  if (!ui.animations || ui.now_ms <= start) return ui.animations ? 0.0f : 1.0f;
  return ease(static_cast<float>(ui.now_ms - start) / duration_ms);
}

int slider_value(Rect r, float x, int min, int max) {
  const float at = std::clamp((x - r.x - slider_thumb / 2) / (r.w - slider_thumb), 0.0f, 1.0f);
  return min + static_cast<int>(std::lround(at * static_cast<float>(max - min)));
}

void Dropdown::paint_button(Ui& ui, Surface* owner, Painter& p, Rect r) const {
  const std::string_view text = selected >= 0 && selected < static_cast<int>(options.size()) ? std::string_view(options[static_cast<std::size_t>(selected)]) : std::string_view{};
  ui.skin().dropdown_button(p, ui.theme, r, text, open, !disabled && ui.hot(owner, id_base), disabled);
  ui.add_hit(owner, r, id_base);
}

void Dropdown::paint_list(Ui& ui, Surface* owner, Painter& p, Rect r) {
  const skin::Screen* screen = ui.pack_screen("dropdown");
  if (!open || !screen) return;
  class Shown final : public skin::Source {
   public:
    Shown(Dropdown& dropdown, Ui& ui, Rect& list, Scroll& scroll) : d_(dropdown), ui_(ui), list_(list), scroll_(scroll) {}
    std::string_view text(int, int index) override { return d_.options[static_cast<std::size_t>(index)]; }
    float number(int id, int index) override {
      switch (id) {
        case Items: return static_cast<float>(d_.options.size());
        case Selected: return index == d_.selected ? 1.0f : 0.0f;
        case Up: return d_.up ? 1.0f : 0.0f;
        case FadeTop: return scroll_.fade_top();
        case FadeBottom: return scroll_.fade_bottom();
        case Heading: return d_.heading(static_cast<std::size_t>(index)) ? 1.0f : 0.0f;
        default: return scroll_.drawn();
      }
    }
    void report(int id, float value) override {
      if (id == View) {
        scroll_.view_h = value;
      } else {
        scroll_.content_h = value;
        scroll_.clamp();
        scroll_.advance(ui_);
      }
    }
    void note(int, int, skin::Box box) override { list_ = rect_of(box); }

   private:
    Dropdown& d_;
    Ui& ui_;
    Rect& list_;
    Scroll& scroll_;
  } shown(*this, ui, list_, scroll_);
  SkinCanvas canvas(ui, p, owner, id_base);
  skin::draw(*ui.pack, *screen, ui.palette, {r.x, r.y, r.w, r.h}, canvas, shown);
}

bool Dropdown::click(int id) {
  if (disabled) return false;
  if (id == id_base) {
    open = !open;
    scroll_ = {};
    return false;
  }
  const int index = id - id_base - 1;
  if (index < 0 || index >= static_cast<int>(options.size()) || heading(static_cast<std::size_t>(index))) return false;
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
    const float longest = ui.motion.scroll_stretch;
    stretch = std::clamp(stretch + past * give * (1 - std::abs(stretch) / longest), -longest, longest);
    pulled_at = ui.now_ms;
  }
}

void Scroll::advance(Ui& ui) {
  edge_fade = ui.motion.edge_fade;
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
  const float settle_ms = ui.motion.scroll_settle_ms, return_ms = ui.motion.scroll_return_ms;
  constexpr float longest_step_ms = 50, near_px = 0.5f;
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

float Scroll::fade_top() const { return std::clamp(drawn(), 0.0f, edge_fade); }

float Scroll::fade_bottom() const { return std::clamp(max_offset() - drawn(), 0.0f, edge_fade); }

Rect Scroll::thumb(Rect track) const {
  const float h = content_h > 0 ? std::max(28.0f, track.h * std::min(1.0f, view_h / content_h)) : track.h;
  const float max = max_offset();
  const float y = max > 0 ? track.y + (track.h - h) * offset / max : track.y;
  return {track.x, y, track.w, h};
}

void Scroll::step_towards(Rect track, float y, float line) {
  if (y < track.y) jump_to(offset - line);
  else if (y >= track.bottom()) jump_to(offset + line);
  else jump_to(y < thumb(track).y ? offset - view_h : offset + view_h);
}

void Scroll::drag_thumb(Rect track, float y) {
  const Rect t = thumb(track);
  const float room = track.h - t.h;
  if (room <= 0) return;
  jump_to((y - t.h / 2 - track.y) / room * max_offset());
}

Rect paint_modal(Ui& ui, Surface* owner, Painter& p, float w, float h, float shown, int backdrop_id, int card_id) {
  p.set_base(D2D1::Matrix3x2F::Identity());
  if (ui.window != 0) {  // it has the window to itself, frame and all
    ui.add_hit_window(owner, ui.content, card_id);
    return ui.content;
  }
  const Rect window{0, 0, ui.width, ui.height};
  ui.skin().backdrop(p, ui.theme, window, shown);
  ui.add_hit_window(owner, ui.modal_area, backdrop_id);
  const Rect settled{ui.content.x + (ui.content.w - w) / 2, ui.content.y + (ui.content.h - h) / 2, w, h};
  const Rect card{settled.x, settled.y + 6 * (1 - shown), w, h};
  ui.skin().panel(p, ui.theme, card, PanelKind::Modal, shown);
  ui.add_hit_window(owner, settled, card_id);
  return card;
}

bool push_edge_fade(Ui& ui, Painter& p, Rect area, float top, float bottom) {
  if ((top <= 0 && bottom <= 0) || !ui.skin().moves()) return false;
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
  ui.skin().button(p, ui.theme, r, label, {.kind = ButtonKind::Modal, .primary = primary, .hot = ui.hot(owner, id), .shown = shown});
  ui.add_hit_window(owner, r, id);
}

}  // namespace crpp::ui
