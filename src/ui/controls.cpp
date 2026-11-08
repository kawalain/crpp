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
  if (const skin::Part* part = ui.pack ? ui.pack->part("dropdown") : nullptr) {
    SkinCanvas canvas(ui, p, nullptr);
    skin::Source nothing;
    const std::pair<std::string_view, float> args[] = {{"open", open ? 1.0f : 0.0f}, {"off", disabled ? 1.0f : 0.0f}};
    skin::draw_part(*ui.pack, *part, ui.palette, {r.x, r.y, r.w, r.h}, canvas, nothing,
                    {.label = text, .hot = !disabled && ui.hot(owner, id_base), .focused = !disabled && ui.focused(owner, id_base), .args = args});
  }
  const skin::HitPlace place{.number = ui.next_hit(), .live = !disabled};
  ui.add_hit(owner, r, id_base, &place);
}

void Dropdown::paint_list(Ui& ui, Surface* owner, Painter& p, Rect r) {
  const skin::Screen* screen = ui.pack_screen("dropdown");
  if (!open || !screen) return;
  class Shown final : public skin::Source {
   public:
    Shown(Dropdown& dropdown, Ui& ui, Rect& list, Scroll& scroll) : d_(dropdown), ui_(ui), list_(list), scroll_(scroll) {}
    std::string_view text(int, int index) override { return index >= 0 && static_cast<std::size_t>(index) < d_.options.size() ? std::string_view(d_.options[static_cast<std::size_t>(index)]) : std::string_view{}; }
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

std::optional<int> Dropdown::key(Ui& ui, Surface* owner, unsigned vk, bool alt) {
  if (disabled) return std::nullopt;
  const int n = static_cast<int>(options.size());
  const auto next_from = [&](int from, int step) {
    for (int i = from + step; i >= 0 && i < n; i += step) {
      if (!heading(static_cast<std::size_t>(i))) return i;
    }
    return -1;
  };
  const bool opener = vk == VK_F4 || (alt && (vk == VK_DOWN || vk == VK_UP));
  int to = -1;
  const int from = open && active_ >= 0 ? active_ : selected;
  switch (vk) {
    case VK_DOWN: to = next_from(from, 1); break;
    case VK_UP: to = next_from(from, -1); break;
    case VK_HOME: to = next_from(-1, 1); break;
    case VK_END: to = next_from(n, -1); break;
    // Eight on, or the nearest option there is that way.
    case VK_NEXT: to = next_from(std::min(from + 7, n - 1), 1) >= 0 ? next_from(std::min(from + 7, n - 1), 1) : next_from(n, -1); break;
    case VK_PRIOR: to = next_from(std::max(from - 7, 0), -1) >= 0 ? next_from(std::max(from - 7, 0), -1) : next_from(-1, 1); break;
    default: break;
  }
  if (!open) {
    if (opener || vk == VK_SPACE) {
      active_ = selected;
      return id_base;
    }
    if (vk == VK_RIGHT) to = next_from(selected, 1);
    if (vk == VK_LEFT) to = next_from(selected, -1);
    if (to < 0) return vk == VK_DOWN || vk == VK_UP || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_HOME || vk == VK_END || vk == VK_NEXT || vk == VK_PRIOR ? std::optional(taken_alone) : std::nullopt;
    return id_base + 1 + to;
  }
  if (opener) return id_base;  // closes
  if (vk == VK_RETURN || vk == VK_SPACE) return active_ >= 0 ? id_base + 1 + active_ : id_base;
  if (to < 0) return vk == VK_DOWN || vk == VK_UP || vk == VK_HOME || vk == VK_END || vk == VK_NEXT || vk == VK_PRIOR ? std::optional(taken_alone) : std::nullopt;
  active_ = to;
  // The pointer's highlight is the one the list has; the keyboard moves it, as in a list of the system.
  ui.hover_owner = owner;
  ui.hover_id = id_base + 1 + to;
  // An option the list cuts off scrolls into view, from where the last frame drew it.
  if (const Ui::Hit* hit = ui.find_stop(owner, id_base + 1 + to)) {
    const float top = ui.to_stage_y(hit->whole.y), bottom = ui.to_stage_y(hit->whole.bottom());
    if (top < list_.y) scroll_.go_to(ui, std::min(scroll_.target, scroll_.offset - (list_.y - top)));
    else if (bottom > list_.bottom()) scroll_.go_to(ui, std::max(scroll_.target, scroll_.offset + (bottom - list_.bottom())));
  }
  return taken_alone;
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

void Scroll::go_to(const Ui& ui, float at) {
  if (!ui.animations) {
    jump_to(at);
    return;
  }
  target = std::clamp(at, 0.0f, max_offset());
  moved_at = ui.now_ms;
}

bool Scroll::key(const Ui& ui, unsigned vk, float line) {
  const float page = std::max(line, view_h - line);
  switch (vk) {
    case VK_UP: go_to(ui, target - line); return true;
    case VK_DOWN: go_to(ui, target + line); return true;
    case VK_PRIOR: go_to(ui, target - page); return true;
    case VK_NEXT: go_to(ui, target + page); return true;
    case VK_HOME: go_to(ui, 0); return true;
    case VK_END: go_to(ui, max_offset()); return true;
    default: return false;
  }
}

std::optional<int> slider_key(unsigned vk, int value, int min, int max) {
  const int tenth = std::max(1, (max - min) / 10);
  int to = value;
  switch (vk) {
    case VK_LEFT:
    case VK_DOWN: to = value - 1; break;
    case VK_RIGHT:
    case VK_UP: to = value + 1; break;
    case VK_PRIOR: to = value + tenth; break;
    case VK_NEXT: to = value - tenth; break;
    case VK_HOME: to = min; break;
    case VK_END: to = max; break;
    default: return std::nullopt;
  }
  return std::clamp(to, min, max);
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

bool push_edge_fade(Ui& ui, Painter& p, Rect area, float top, float bottom) {
  if ((top <= 0 && bottom <= 0) || !ui.moves()) return false;
  ID2D1LinearGradientBrush* mask = ui.edge_fade_mask(p, area, top, bottom);
  if (!mask) return false;
  mask->SetStartPoint(D2D1::Point2F(0, area.y));
  mask->SetEndPoint(D2D1::Point2F(0, area.bottom()));
  p.rt()->PushLayer(D2D1::LayerParameters(D2D1::RectF(area.x, area.y, area.right(), area.bottom()), nullptr,
                                          D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), 1.0f, mask),
                    nullptr);
  return true;
}

}  // namespace crpp::ui
