#include "crpp/ui/surface.hpp"

#include <windows.h>

#include <algorithm>
#include <ctime>

#include "crpp/locale/locale.hpp"
#include "crpp/skin/pack.hpp"
#include "crpp/ui/toasts.hpp"

namespace crpp::ui {

namespace {
constexpr const char* default_emoji = "1f4a7";
}  // namespace

void Ui::begin_frame(float window_w, float window_h, unsigned long long now) {
  width = window_w;
  height = window_h;
  now_ms = now;
  if (scales_stage()) {
    scale = std::max(window_w / stage_w, window_h / stage_h);
    ox = (window_w - stage_w * scale) / 2;
    oy = (window_h - stage_h * scale) / 2;
  } else {
    scale = 1;
    ox = oy = 0;
  }
  content = modal_area = {0, 0, window_w, window_h};
  hits_.clear();
  hit_clips_.clear();
  transitions.next_frame();
}

const skin::Screen* Ui::pack_screen(std::string_view name) const {
  return pack && palette >= 0 ? pack->screen(name) : nullptr;
}

bool Ui::scales_stage() const { return !pack || pack->constant("own_frame") == 0; }

bool Ui::moves() const { return pack && pack->constant("moves") != 0; }

Rect Ui::screen_area() const { return scales_stage() ? Rect{0, 0, stage_w, stage_h} : content; }

void Ui::add_hit(Surface* owner, Rect r, int id) { add_hit_window(owner, to_window(r), id); }

void Ui::add_hit_window(Surface* owner, Rect r, int id) {
  if (!hits_enabled) return;
  for (const Rect& clip : hit_clips_) {
    const float left = std::max(r.x, clip.x), top = std::max(r.y, clip.y);
    const float right = std::min(r.right(), clip.right()), bottom = std::min(r.bottom(), clip.bottom());
    if (right <= left || bottom <= top) return;
    r = {left, top, right - left, bottom - top};
  }
  hits_.push_back({r, owner, id});
}

void Ui::add_hit_shape(Surface* owner, Rect r, int id, std::string_view data, float x, float y) {
  const std::size_t before = hits_.size();
  add_hit(owner, r, id);
  if (hits_.size() == before) return;
  hits_.back().shape = data;
  hits_.back().shape_x = x;
  hits_.back().shape_y = y;
}

void Ui::push_hit_clip(Rect window_rect) { hit_clips_.push_back(window_rect); }

void Ui::pop_hit_clip() { hit_clips_.pop_back(); }

const Ui::Hit* Ui::hit_at(float x, float y) const {
  // Later hits are drawn on top.
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
    if (!it->rect.contains(x, y) || !it->owner->accepts(it->id, x, y)) continue;
    if (!it->shape.empty()) {
      BOOL inside = FALSE;
      if (ID2D1PathGeometry* const shape = gfx.path(it->shape)) {
        shape->FillContainsPoint(D2D1::Point2F(to_stage_x(x), to_stage_y(y)), D2D1::Matrix3x2F::Translation(it->shape_x, it->shape_y), &inside);
      }
      if (!inside) continue;
    }
    return &*it;
  }
  return nullptr;
}

namespace {
unsigned long long held_clock = 0;
}

unsigned long long clock_ms() noexcept {
  if (held_clock != 0) return held_clock;
  static const auto frequency = [] {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
  }();
  LARGE_INTEGER counter{};
  QueryPerformanceCounter(&counter);
  return static_cast<unsigned long long>(counter.QuadPart / frequency * 1000 + counter.QuadPart % frequency * 1000 / frequency);
}

void hold_clock_at(unsigned long long ms) noexcept { held_clock = ms; }

namespace {
long long held_wall_clock = 0;
}  // namespace

long long wall_clock_seconds() noexcept {
  return held_wall_clock != 0 ? held_wall_clock : static_cast<long long>(std::time(nullptr));
}

void hold_wall_clock(long long seconds) noexcept { held_wall_clock = seconds; }

const Ui::Hit* Ui::find_hit(const Surface* owner, int id) const {
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
    if (it->owner == owner && it->id == id) return &*it;
  }
  return nullptr;
}

ID2D1LinearGradientBrush* Ui::edge_fade_mask(Painter& p, Rect area, float top, float bottom) {
  if (masks_target_ != p.rt()) {
    fade_masks_.clear();
    masks_target_ = p.rt();
  }
  const int top_px = static_cast<int>(top + 0.5f), bottom_px = static_cast<int>(bottom + 0.5f);
  const int key = (static_cast<int>(area.h) << 12) | (top_px << 6) | bottom_px;
  for (FadeMask& mask : fade_masks_) {
    if (mask.key == key) return mask.brush.get();
  }
  constexpr std::size_t most = 128;  // one list scrolling needs a few dozen
  if (fade_masks_.size() >= most) fade_masks_.clear();
  const auto stop = [](float at, float alpha) { return D2D1::GradientStop(at, D2D1::ColorF(0, 0, 0, alpha)); };
  const float h = std::max(area.h, 1.0f);
  const D2D1_GRADIENT_STOP stops[4] = {stop(0, top_px > 0 ? 0.0f : 1.0f), stop(static_cast<float>(top_px) / h, 1),
                                       stop(1 - static_cast<float>(bottom_px) / h, 1), stop(1, bottom_px > 0 ? 0.0f : 1.0f)};
  ComPtr<ID2D1GradientStopCollection> collection;
  ComPtr<ID2D1LinearGradientBrush> brush;
  if (FAILED(p.rt()->CreateGradientStopCollection(stops, 4, collection.put())) ||
      FAILED(p.rt()->CreateLinearGradientBrush(
          D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, area.y), D2D1::Point2F(0, area.bottom())), collection.get(),
          brush.put()))) {
    return nullptr;
  }
  fade_masks_.push_back({key, std::move(brush)});
  return fade_masks_.back().brush.get();
}

void Ui::forget_render_target() {
  gfx.forget_render_target();
  fade_masks_.clear();
  masks_target_ = nullptr;
}

const SvgImage& Ui::emoji_image(std::string_view id) {
  const SvgImage* image = &emoji->get(gfx, id.empty() ? std::string(default_emoji) : std::string(id));
  if (image->empty()) image = &emoji->get(gfx, default_emoji);  // unknown emoji id
  return *image;
}

bool Ui::copy(std::wstring_view text) {
  if (clipboard && clipboard->set_text(text)) return true;
  if (toasts) toasts->toast(tr().copy_failed);
  return false;
}

void Ui::paste_into(TextField& field) {
  if (!clipboard) return;
  if (const auto text = clipboard->text()) field.paste(*text);
}

bool Ui::edit_key(TextField& field, unsigned vk, bool ctrl, bool shift) {
  if (ctrl && (vk == 'C' || vk == 'X' || vk == VK_INSERT) && !shift) {
    // Ctrl+Insert is the older copy, as in any Windows field.
    if (field.can_copy() && copy(field.selected_text()) && vk == 'X') field.erase();
    return true;
  }
  if ((ctrl && vk == 'V' && !shift) || (shift && !ctrl && vk == VK_INSERT)) {
    paste_into(field);
    return true;
  }
  if (shift && !ctrl && vk == VK_DELETE) {  // Shift+Delete cuts
    if (field.can_copy() && copy(field.selected_text())) field.erase();
    return true;
  }
  return field.key_down(vk, ctrl, shift);
}

std::vector<MenuItem> Ui::field_menu(const TextField& field) {
  return {{MenuCommand::Cut, field.can_copy()},
          {MenuCommand::Copy, field.can_copy()},
          {MenuCommand::Paste, clipboard && clipboard->has_text()},
          {MenuCommand::Delete, field.can_delete()},
          {MenuCommand::SelectAll, !field.text().empty()}};
}

void Ui::field_command(TextField& field, MenuCommand command) {
  switch (command) {
    case MenuCommand::Cut:
      if (field.can_copy() && copy(field.selected_text())) field.erase();
      break;
    case MenuCommand::Copy:
      if (field.can_copy()) copy(field.selected_text());
      break;
    case MenuCommand::Paste: paste_into(field); break;
    case MenuCommand::Delete: field.erase(); break;
    case MenuCommand::SelectAll: field.select_all(); break;
    case MenuCommand::CopyLink: break;
  }
}

bool Surface::key(unsigned vk, bool ctrl, bool shift) { return ui_.focus && ui_.edit_key(*ui_.focus, vk, ctrl, shift); }

}  // namespace crpp::ui
