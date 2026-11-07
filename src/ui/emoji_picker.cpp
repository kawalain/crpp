#include "crpp/ui/emoji_picker.hpp"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace crpp::ui {

namespace {

constexpr int columns = 8;
constexpr float picker_w = 400, picker_h = 306, pad = 14, gap = 4;
constexpr float cell = (picker_w - 2 * pad - (columns - 1) * gap) / columns;
constexpr float emoji_size = 32;
constexpr float thumb_in_ms = 150, thumb_out_ms = 500;
constexpr unsigned long long thumb_hide_ms = 2000;

}  // namespace

void EmojiPicker::show(std::string current, std::function<void(std::string id)> on_pick) {
  open_ = true;
  closed_at_ = 0;
  opened_at_ = clock_ms();
  current_ = std::move(current);
  on_pick_ = std::move(on_pick);
  scroll_ = {};
  thumb_on_ = false;
  thumb_hide_at_ = 0;
  const std::filesystem::path dir = ui_.emoji ? ui_.emoji->dir() : std::filesystem::path{};
  cache_.emplace(dir, cache_capacity);
  ids_ = {};
  for (const std::string& id : ui_.emoji_ids) {
    if (cache_->available(ui_.gfx, id)) ids_.push_back(id);
  }
  ui_.set_focus(nullptr, nullptr);
}

void EmojiPicker::close() {
  open_ = false;
  on_pick_ = nullptr;
  if (ui_.capture == this) ui_.capture = nullptr;
  // It fades out as it was, so what it shows stays until then.
  if (ui_.animations && cache_) closed_at_ = clock_ms();
  else release_shown();
}

void EmojiPicker::forget_graphics() {
  if (!cache_) return;
  auto dir = cache_->dir();  // what emplace is about to destroy
  cache_.emplace(std::move(dir), cache_capacity);
}

void EmojiPicker::release_shown() {
  closed_at_ = 0;
  cache_.reset();
  ids_ = {};
}

void EmojiPicker::reveal_thumb() {
  const auto now = clock_ms();
  if (!thumb_on_) thumb_since_ = now;
  thumb_on_ = true;
  thumb_hide_at_ = now + thumb_hide_ms;
}

void EmojiPicker::paint(Painter& p) {
  if (!open_ && closed_at_ == 0) return;
  const float shown = open_ ? transition(ui_, opened_at_, modal_pop_ms) : 1 - transition(ui_, closed_at_, modal_pop_ms);
  if (!open_ && shown <= 0) {
    release_shown();
    return;
  }
  const bool moving = shown < 1;
  if (moving) {
    ui_.more_frames = true;
    p.push_opacity(shown);
    p.set_moved(0, 6 * (1 - shown));
  }
  ui_.hits_enabled = open_;
  paint_shown(p);
  ui_.hits_enabled = true;
  if (moving) {
    p.set_moved(0, 0);
    p.pop_opacity();
  }
}

void EmojiPicker::paint_shown(Painter& p) {
  const Theme& t = ui_.theme;
  p.set_base(D2D1::Matrix3x2F::Identity());
  const Rect window{0, 0, ui_.width, ui_.height};
  if (ui_.skin().moves()) p.fill_rect(window, rgb(0x365669, 0.28f));
  ui_.add_hit_window(this, ui_.modal_area, Backdrop);
  const Rect r{ui_.content.x + (ui_.content.w - picker_w) / 2, ui_.content.y + (ui_.content.h - picker_h) / 2, picker_w, picker_h};
  ui_.skin().panel(p, t, r, PanelKind::Picker);
  ui_.add_hit_window(this, r, Card);

  const Rect view{r.x + pad, r.y + pad, picker_w - 2 * pad, picker_h - 2 * pad};
  const int rows = (static_cast<int>(ids_.size()) + columns - 1) / columns;
  scroll_.view_h = view.h;
  scroll_.content_h = rows > 0 ? static_cast<float>(rows) * (cell + gap) - gap : 0;
  scroll_.clamp();
  scroll_.advance(ui_);

  p.rt()->PushAxisAlignedClip(D2D1::RectF(view.x, view.y, view.right(), view.bottom()), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  const bool fade = push_edge_fade(ui_, p, view, scroll_.fade_top(), scroll_.fade_bottom());
  ui_.push_hit_clip(view);
  // Only the rows in view are touched, so only they get parsed.
  const int first_row = std::max(0, static_cast<int>(scroll_.drawn() / (cell + gap)));
  const int last_row = std::min(rows - 1, static_cast<int>((scroll_.drawn() + view.h) / (cell + gap)));
  for (int row = first_row; row <= last_row; ++row) {
    for (int col = 0; col < columns; ++col) {
      const auto index = static_cast<std::size_t>(row * columns + col);
      if (index >= ids_.size()) break;
      const Rect box{view.x + static_cast<float>(col) * (cell + gap),
                     view.y + static_cast<float>(row) * (cell + gap) - scroll_.drawn(), cell, cell};
      const int id = CellBase + static_cast<int>(index);
      ui_.skin().cell(p, t, box, ids_[index] == current_, ui_.hot(this, id));
      const float inset = (cell - emoji_size) / 2;
      draw_svg(p, cache_->get(ui_.gfx, ids_[index]), {box.x + inset, box.y + inset, emoji_size, emoji_size});
      ui_.add_hit_window(this, box, id);
    }
  }
  ui_.pop_hit_clip();
  pop_edge_fade(p, fade);
  p.rt()->PopAxisAlignedClip();

  track_ = {r.right() - 4 - 8, r.y + pad, 8, picker_h - 2 * pad};
  if (scroll_.max_offset() > 0) {
    ui_.add_hit_window(this, track_, Track);
    const Rect thumb = scroll_.thumb(track_);
    ui_.add_hit_window(this, thumb, Thumb);
    const bool over = ui_.hot(this, Track) || ui_.hot(this, Thumb) || ui_.capture == this;
    if (over && !thumb_on_) reveal_thumb();
    const float moved = transition(ui_, thumb_since_, thumb_on_ ? thumb_in_ms : thumb_out_ms);
    // A Skin that does not move always shows it.
    const float alpha = !ui_.skin().moves() ? 1 : thumb_on_ ? moved : (thumb_since_ == 0 ? 0 : 1 - moved);
    if (alpha > 0) {
      ui_.skin().scrollbar(p, t, thumb, over ? 1.0f : 0.0f, alpha);
    }
  }
}

void EmojiPicker::click(int id, float, float y, bool) {
  if (id == Backdrop) {
    close();
    return;
  }
  if (id == Track || id == Thumb) {
    ui_.capture = this;
    scroll_.drag_thumb(track_, y);
    reveal_thumb();
    return;
  }
  if (id < CellBase) return;
  const auto index = static_cast<std::size_t>(id - CellBase);
  if (index >= ids_.size()) return;
  // Moved out first: close drops them, and the callback may open something.
  std::string picked = ids_[index];
  const auto pick = std::move(on_pick_);
  close();
  if (pick) pick(std::move(picked));
}

void EmojiPicker::drag(float, float y) {
  scroll_.drag_thumb(track_, y);
  reveal_thumb();
}

void EmojiPicker::release() { reveal_thumb(); }

bool EmojiPicker::wheel(float, float, float delta) {
  if (!open_) return false;
  scroll_.wheel(ui_, delta);
  reveal_thumb();
  return true;
}

bool EmojiPicker::escape() {
  if (!open_) return false;
  close();
  return true;
}

Cursor EmojiPicker::cursor(int id) const {
  return id == Thumb || id == Track || id >= CellBase ? Cursor::Hand : Cursor::Arrow;
}

bool EmojiPicker::animating() const {
  if (!open_ || !ui_.animations || thumb_since_ == 0) return false;
  const float elapsed = static_cast<float>(ui_.now_ms - std::min(ui_.now_ms, thumb_since_));
  return elapsed < (thumb_on_ ? thumb_in_ms : thumb_out_ms);
}

bool EmojiPicker::tick(unsigned long long now_ms) {
  if (!open_) return false;
  bool changed = animating();
  if (thumb_on_ && thumb_hide_at_ != 0 && now_ms >= thumb_hide_at_) {
    if (ui_.hot(this, Track) || ui_.hot(this, Thumb) || ui_.capture == this) {
      thumb_hide_at_ = now_ms + thumb_hide_ms;  // still under the pointer
    } else {
      thumb_on_ = false;
      thumb_since_ = now_ms;
      thumb_hide_at_ = 0;
      changed = true;
    }
  }
  return changed;
}

unsigned long long EmojiPicker::wake_at() const { return open_ && thumb_on_ ? thumb_hide_at_ : 0; }

}  // namespace crpp::ui
