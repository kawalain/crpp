#include "crpp/ui/emoji_picker.hpp"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum NumberId : int { Count, Offset, Drawn, Overflow, Selected, Open, FadeTop, FadeBottom, ThumbOn, ThumbOver, ThumbY, ThumbH };
enum ReportId : int { View, Content, First, Width, Height, ShownReport };
constexpr skin::Name number_names[] = {{"count", Count}, {"offset", Offset}, {"scroll", Drawn}, {"overflow", Overflow}, {"selected", Selected}, {"open", Open},
                                       {"fade_top", FadeTop}, {"fade_bottom", FadeBottom}, {"thumb_on", ThumbOn}, {"thumb_over", ThumbOver}, {"thumb_y", ThumbY}, {"thumb_h", ThumbH}};
constexpr skin::Name picture_names[] = {{"cell", 0}};
constexpr skin::Name hit_names[] = {{"backdrop", EmojiPicker::Backdrop}, {"card", EmojiPicker::Card}, {"track", EmojiPicker::Track}, {"thumb", EmojiPicker::Thumb}, {"cell", EmojiPicker::CellBase}};
constexpr skin::Name report_names[] = {{"view", View}, {"content", Content}, {"first", First}, {"width", Width}, {"height", Height}, {"shown", ShownReport}};
constexpr skin::Name note_names[] = {{"track", 0}};

constexpr unsigned long long thumb_hide_ms = 2000;

}  // namespace

skin::ScreenSchema EmojiPicker::schema() { return {.screen = "emoji", .numbers = number_names, .pictures = picture_names, .hits = hit_names, .reports = report_names, .notes = note_names}; }

D2D1_SIZE_F EmojiPicker::content_size() const {
  const skin::Screen* const screen = ui_.pack_screen("emoji");
  if (!screen) return {0, 0};
  class Sized final : public skin::Source {
   public:
    void report(int id, float value) override {
      if (id == Width) size.width = value;
      else if (id == Height) size.height = value;
    }
    D2D1_SIZE_F size{};
  } sized;
  measure_screen(ui_, *screen, ui_.content, sized);
  return sized.size;
}

void EmojiPicker::paint_pack(Painter& p, const skin::Screen& screen) {
  class Shown final : public skin::Source {
   public:
    Shown(EmojiPicker& picker, Painter& painter) : e_(picker), p_(painter) {}
    float number(int id, int index) override {
      switch (id) {
        case Count: return static_cast<float>(e_.ids_.size());
        case Offset: return e_.scroll_.offset;
        case Drawn: return e_.scroll_.drawn();
        case Overflow: return e_.scroll_.max_offset() > 0 ? 1.0f : 0.0f;
        case Open: return e_.open_ ? 1.0f : 0.0f;
        case FadeTop: return e_.scroll_.fade_top();
        case FadeBottom: return e_.scroll_.fade_bottom();
        case ThumbOn: return e_.thumb_on_ ? 1.0f : 0.0f;
        case ThumbOver: return e_.ui_.hot(&e_, Track) || e_.ui_.hot(&e_, Thumb) || e_.ui_.capture == &e_ ? 1.0f : 0.0f;
        case ThumbY: return e_.scroll_.thumb(e_.track_).y;
        case ThumbH: return e_.scroll_.thumb(e_.track_).h;
        default: {
          const std::string* const at = id_at(index);
          return at && *at == e_.current_ ? 1.0f : 0.0f;
        }
      }
    }
    void picture(int, int index, skin::Box box, float opacity, float gray) override {
      if (const std::string* const at = id_at(index)) draw_svg(p_, e_.cache_->get(e_.ui_.gfx, *at), rect_of(box), opacity, gray);
    }
    void report(int id, float value) override {
      if (id == View) {
        e_.scroll_.view_h = value;
      } else if (id == Content) {
        e_.scroll_.content_h = value;
        e_.scroll_.clamp();
        e_.scroll_.advance(e_.ui_);
      } else if (id == First) {
        // The pack's number: anything outside the list, NaN included, starts at the end or the top.
        first_ = value >= 0 ? (value < static_cast<float>(e_.ids_.size()) ? static_cast<std::size_t>(value) : e_.ids_.size()) : 0;
      } else if (id == ShownReport) {
        e_.shown_left_ = value;
      }
    }
    void note(int, int, skin::Box box) override { e_.track_ = rect_of(box); }

   private:
    // The emoji at a round of the pack's repeat, which may count past the list.
    [[nodiscard]] const std::string* id_at(int index) const {
      if (index < 0 || static_cast<std::size_t>(index) >= e_.ids_.size() - first_) return nullptr;
      return &e_.ids_[first_ + static_cast<std::size_t>(index)];
    }

    EmojiPicker& e_;
    Painter& p_;
    std::size_t first_ = 0;  // never past the end of the list
  } shown(*this, p);
  if ((ui_.hot(this, Track) || ui_.hot(this, Thumb) || ui_.capture == this) && !thumb_on_) reveal_thumb();
  shown_left_ = 0;
  draw_layer(ui_, p, this, screen, ui_.content, shown);
}

void EmojiPicker::show(std::string current, std::function<void(std::string id)> on_pick) {
  open_ = true;
  closed_at_ = 0;
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

std::optional<Role> EmojiPicker::role(int id) const {
  if (id < CellBase) return std::nullopt;
  const auto in_use = std::ranges::find(ids_, current_);
  const int cell = in_use == ids_.end() ? 0 : static_cast<int>(in_use - ids_.begin());
  return id == CellBase + cell ? std::optional(Role::List) : std::nullopt;
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
  thumb_on_ = true;
  thumb_hide_at_ = clock_ms() + thumb_hide_ms;
}

void EmojiPicker::paint(Painter& p) {
  if (!open_ && closed_at_ == 0) return;
  const skin::Screen* screen = ui_.pack_screen("emoji");
  if (!screen) return;
  p.set_base(D2D1::Matrix3x2F::Identity());
  ui_.hits_enabled = open_;
  paint_pack(p, *screen);
  ui_.hits_enabled = true;
  if (!open_ && shown_left_ <= 0) release_shown();
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

bool EmojiPicker::tick(unsigned long long now_ms) {
  if (!open_) return false;
  bool changed = false;
  if (thumb_on_ && thumb_hide_at_ != 0 && now_ms >= thumb_hide_at_) {
    if (ui_.hot(this, Track) || ui_.hot(this, Thumb) || ui_.capture == this) {
      thumb_hide_at_ = now_ms + thumb_hide_ms;  // still under the pointer
    } else {
      thumb_on_ = false;
      thumb_hide_at_ = 0;
      changed = true;
    }
  }
  return changed;
}

unsigned long long EmojiPicker::wake_at() const { return open_ && thumb_on_ ? thumb_hide_at_ : 0; }

}  // namespace crpp::ui
