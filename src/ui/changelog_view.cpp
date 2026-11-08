#include "crpp/ui/changelog_view.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

constexpr float marker_w = 20;                       // what a numbered item's number has
constexpr unsigned long long idle_reset_ms = 1800;    // IDLE_RESET
constexpr unsigned long long switch_cooldown_ms = 500;  // SWITCH_COOLDOWN

ComPtr<IDWriteTextLayout> runs_layout(const Gfx& gfx, const std::vector<update::NoteRun>& runs, const TextStyle& style, float width) {
  std::wstring text;
  std::vector<DWRITE_TEXT_RANGE> bold;
  for (const update::NoteRun& run : runs) {
    const std::wstring piece = widen(run.text);
    if (run.bold) bold.push_back({static_cast<UINT32>(text.size()), static_cast<UINT32>(piece.size())});
    text += piece;
  }
  auto layout = gfx.layout(text, style, width);
  if (layout) {
    for (const DWRITE_TEXT_RANGE& range : bold) layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
  }
  return layout;
}

float layout_height(IDWriteTextLayout* layout) {
  DWRITE_TEXT_METRICS m{};
  if (layout) layout->GetMetrics(&m);
  return m.height;
}

enum TextId : int { Tagline, Date, Version, Hint };
enum NumberId : int { HasEntries, HasTagline, HasDate, HasVersion, ShownHint, Overflow, View, Content, Offset, HintOn };
constexpr skin::Name text_names[] = {{"tagline", Tagline}, {"date", Date}, {"version", Version}, {"hint", Hint}};
constexpr skin::Name number_names[] = {{"has_entries", HasEntries}, {"has_tagline", HasTagline}, {"has_date", HasDate}, {"has_version", HasVersion}, {"shown_hint", ShownHint},
                                       {"overflow", Overflow}, {"view", View}, {"content", Content}, {"offset", Offset}, {"hint_on", HintOn}};
constexpr skin::Name hit_names[] = {{"backdrop", ChangelogView::Backdrop}, {"close", ChangelogView::Close}, {"card", ChangelogView::Card}, {"bar_track", ChangelogView::BarTrack}, {"bar_thumb", ChangelogView::BarThumb}, {"bar_up", ChangelogView::BarUp}, {"bar_down", ChangelogView::BarDown}};
constexpr skin::Name slot_names[] = {{"body", 0}};
constexpr skin::Name note_names[] = {{"bar_track", 0}};
constexpr skin::Name report_names[] = {{"hint_left", 0}};

}  // namespace

skin::ScreenSchema ChangelogView::schema() {
  return {.screen = "changelog", .texts = text_names, .numbers = number_names, .hits = hit_names, .slots = slot_names, .reports = report_names, .notes = note_names};
}

ChangelogOverscroll::Result ChangelogOverscroll::step(int direction, unsigned long long now, bool can_go, bool at_edge, float notches) {
  if (direction == 0 || !can_go || !at_edge) {
    reset();
    return {};
  }
  if (direction != direction_) {
    reset();
    direction_ = direction;
  }
  const int inputs = notches >= 0.4f ? std::max(1, static_cast<int>(std::lround(notches)))
                     : last_ == 0 || now - last_ > wheel_gap_ms ? 1
                                                                : 0;
  last_ = now;
  if (!shown_) {
    count_ += inputs;
    if (count_ < hint_at) return {};
    shown_ = true;
    shown_at_ = now;
    return {direction, 0};
  }
  // The wheel does not count while the hint is still appearing.
  if (now - shown_at_ < hint_ms) return {direction, 0};
  after_ += inputs;
  if (after_ >= switch_after) {
    reset();
    return {0, direction};
  }
  return {direction, 0};
}

void ChangelogView::open(std::vector<ChangelogEntry> entries, std::size_t index) {
  entries_ = std::move(entries);
  open_ = true;
  was_animating_ = true;
  ui_.set_focus(nullptr, nullptr);
  show(entries_.empty() ? 0 : std::min(index, entries_.size() - 1), false);
}

void ChangelogView::set_entries(std::vector<ChangelogEntry> entries) {
  const std::string current = index_ < entries_.size() ? entries_[index_].version : std::string{};
  entries_ = std::move(entries);
  std::size_t index = 0;
  for (std::size_t i = 0; i < entries_.size(); ++i) {
    if (entries_[i].version == current) index = i;
  }
  show(index, false);
}

void ChangelogView::close() {
  open_ = false;
  // Nothing of this stays in memory while it is closed.
  entries_ = {};
  lines_ = {};
  selection_.clear();
  built_ = false;
  idle_at_ = 0;
  hint_ = hint_shown_ = 0;
  overscroll_.reset();
}

void ChangelogView::show(std::size_t index, bool at_bottom) {
  index_ = index;
  selection_.clear();
  built_ = false;
  scroll_ = {};
  start_at_bottom_ = at_bottom;
  overscroll_.reset();
  idle_at_ = 0;
  show_hint(0);
}

void ChangelogView::show_hint(int direction) {
  if (direction == hint_) return;
  hint_ = direction;
  if (direction != 0) hint_shown_ = direction;
  was_animating_ = true;
}

void ChangelogView::lay_out_body(const BodyLook& look, float width) {
  built_ = true;
  built_width_ = width;
  lines_.clear();
  body_h_ = 0;
  if (index_ >= entries_.size()) return;
  const Gfx& gfx = ui_.gfx;
  // .cl-body.
  float y = look.top;
  bool first = true;
  for (const update::NoteBlock& block : entries_[index_].blocks) {
    Line line;
    if (block.kind == update::NoteKind::Heading) {
      y += first ? 0 : look.heading_top;
      line.x = look.pad_x;
      line.y = y;
      line.layout = runs_layout(gfx, block.runs, look.heading, width - 2 * look.pad_x);
      for (const update::NoteRun& run : block.runs) line.text += widen(run.text);
      y += layout_height(line.layout.get()) + look.heading_bottom;
    } else {
      const float indent = block.kind == update::NoteKind::Bullet ? look.indent * static_cast<float>(block.indent + 1) : 0;
      y += look.margin;
      line.x = look.pad_x + indent;
      line.y = y;
      line.layout = runs_layout(gfx, block.runs, look.item, width - 2 * look.pad_x - indent);
      for (const update::NoteRun& run : block.runs) line.text += widen(run.text);
      if (block.kind == update::NoteKind::Bullet) {
        if (block.marker.empty()) {
          line.bullet_x = line.x - 11;
        } else {
          line.marker = widen(block.marker);
          line.marker_x = line.x - look.indent;
        }
      }
      y += layout_height(line.layout.get()) + look.margin;
    }
    lines_.push_back(std::move(line));
    first = false;
  }
  body_h_ = y + look.bottom;
}

void ChangelogView::scroll_body() {
  scroll_.content_h = body_h_;
  scroll_.clamp();
  if (start_at_bottom_) {
    scroll_.jump_to(scroll_.max_offset());
    start_at_bottom_ = false;
  }
  scroll_.advance(ui_);
}

void ChangelogView::paint_body(Painter& p, Rect body, const TextStyle& item, float marker_h, Color ink, Color selection) {
  body_ = body;
  // The body scrolls under the head, which stays (.cl-head is sticky).
  p.rt()->PushAxisAlignedClip(D2D1::RectF(body.x, body.y, body.right(), body.bottom()), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  const bool fade = push_edge_fade(ui_, p, body, scroll_.fade_top(), scroll_.fade_bottom());
  ui_.push_hit_clip(body);
  for (std::size_t i = 0; i < lines_.size(); ++i) {
    const Line& line = lines_[i];
    const float y = body.y + line.y - scroll_.drawn();
    if (line.layout) {
      DWRITE_TEXT_METRICS m{};
      line.layout->GetMetrics(&m);
      ui_.add_hit_window(this, {body.x + line.x, y, line.layout->GetMaxWidth(), m.height}, Text);
      if (const auto part = selection_.in_block(i, line.text.size())) {
        p.fill_text_range(line.layout.get(), body.x + line.x, y, part->first, part->second, selection);
      }
    }
    if (line.bullet_x >= 0) p.fill_ellipse(body.x + line.bullet_x, y + first_line_ / 2, 2.5f, 2.5f, ink);
    if (!line.marker.empty()) {
      p.text(line.marker, {body.x + line.marker_x - 4, y, marker_w, marker_h}, item, ink);
    }
    if (line.layout) p.draw_layout(line.layout.get(), body.x + line.x, y, ink);
  }
  ui_.pop_hit_clip();
  pop_edge_fade(p, fade);
  p.rt()->PopAxisAlignedClip();
}

void ChangelogView::paint_pack(Painter& p, const skin::Screen& screen) {
  class Shown final : public skin::Source {
   public:
    Shown(ChangelogView& view, Painter& painter) : v_(view), p_(painter) {
      if (v_.index_ < v_.entries_.size()) entry_ = &v_.entries_[v_.index_];
      // Going, it stays until the screen says its fade is over. What does not move is gone at once.
      if (v_.hint_ != 0 || v_.ui_.animations) hint_ = v_.hint_shown_;
    }
    std::string_view text(int id, int) override {
      switch (id) {
        case Tagline: return entry_ ? std::string_view(entry_->tagline) : std::string_view{};
        case Date: return entry_ ? std::string_view(entry_->date) : std::string_view{};
        case Version: return entry_ ? std::string_view(entry_->version) : std::string_view{};
        default: return hint_ < 0 ? tr().changelog_newer_hint : tr().changelog_older_hint;
      }
    }
    float number(int id, int) override {
      const auto yes = [](bool b) { return b ? 1.0f : 0.0f; };
      switch (id) {
        case HasEntries: return yes(entry_ != nullptr);
        case HasTagline: return yes(entry_ && !entry_->tagline.empty());
        case HasDate: return yes(entry_ && !entry_->date.empty());
        case HasVersion: return yes(entry_ && !entry_->version.empty());
        case ShownHint: return static_cast<float>(hint_);
        case Overflow: return yes(v_.scroll_.max_offset() > 0);
        case HintOn: return yes(v_.hint_ != 0);
        case View: return v_.scroll_.view_h;
        case Content: return v_.scroll_.content_h;
        default: return v_.scroll_.offset;
      }
    }
    void slot(int, int, skin::Box box, const skin::SlotValues& values) override {
      const auto n = values.numbers;
      const float width = n[7];
      const TextStyle item = text_style_of(values.styles[1]);
      v_.first_line_ = item.line_height;
      if (!v_.built_ || v_.built_width_ != width) {
        v_.lay_out_body({n[0], n[1], n[2], n[3], n[4], n[5], n[6], text_style_of(values.styles[0]), item}, width);
      }
      v_.scroll_.view_h = box.h;
      v_.scroll_body();
      v_.scroll_while_selecting();
      // A pack of format 1 names no selection colour: the ink, lighter.
      const Color selection = values.colors.size() > 1 ? values.colors[1] : with_alpha(values.colors[0], 0.25f);
      v_.paint_body(p_, rect_of(box), item, item.line_height, values.colors[0], selection);
      if (v_.selecting_ && v_.scrolled_at_ != 0) {
        v_.selection_.drag_to(v_.point_at(v_.drag_x_, v_.drag_y_), [this](std::size_t n) { return v_.text_of(n); });
      }
    }
    void note(int, int, skin::Box box) override { v_.bar_track_ = rect_of(box); }
    void report(int, float value) override {
      if (v_.hint_ == 0 && value <= 0) v_.hint_shown_ = 0;
    }

   private:
    ChangelogView& v_;
    Painter& p_;
    const ChangelogEntry* entry_ = nullptr;
    int hint_ = 0;
  } shown(*this, p);
  draw_layer(ui_, p, this, screen, ui_.content, shown);
}

void ChangelogView::paint(Painter& p) {
  if (!open_) return;
  if (const skin::Screen* screen = ui_.pack_screen("changelog")) {
    p.set_base(D2D1::Matrix3x2F::Identity());
    paint_pack(p, *screen);
  }
}

void ChangelogView::click(int id, float x, float y, bool shift) {
  if (id == Text) {
    // The notes take the keys now: Ctrl+C copies what is selected, Ctrl+A selects them all.
    ui_.selection_owner = this;
    ui_.capture = this;
    selecting_ = true;
    drag_x_ = x;
    drag_y_ = y;
    scrolled_at_ = 0;
    selection_.press(point_at(x, y), ui_.clicks, shift, [this](std::size_t n) { return text_of(n); });
  } else if (id == Close || id == Backdrop) {
    close();
  } else if (repeats(id)) {
    scroll_.step_towards(bar_track_, y);
  } else if (id == BarThumb) {
    ui_.capture = this;
    grab_y_ = y;
    grab_offset_ = scroll_.offset;
  }
}

void ChangelogView::drag(float x, float y) {
  if (selecting_) {
    drag_x_ = x;
    drag_y_ = y;
    selection_.drag_to(point_at(x, y), [this](std::size_t n) { return text_of(n); });
    return;
  }
  const float travel = std::max(1.0f, bar_track_.h - scroll_.thumb(bar_track_).h);
  scroll_.jump_to(grab_offset_ + (y - grab_y_) * scroll_.max_offset() / travel);
}

bool ChangelogView::stop_key(int id, unsigned vk, bool ctrl, bool, bool) {
  if (id != Text || ctrl || !open_) return false;
  return scroll_.key(ui_, vk, first_line_ > 0 ? first_line_ : 20);
}

bool ChangelogView::wheel(float, float, float delta) {
  if (!open_) return false;
  const auto now = clock_ms();
  const int direction = delta < 0 ? 1 : delta > 0 ? -1 : 0;
  if (direction == 0 || (ui_.animations && now < ignore_until_)) return true;
  const bool can_go = direction > 0 ? index_ + 1 < entries_.size() : index_ > 0;
  const bool at_edge = direction > 0 ? scroll_.target >= scroll_.max_offset() - 1 : scroll_.target <= 1;
  // With animations off (tests, --shot) the wait for the hint has already passed.
  const auto clock = ui_.animations ? now : now + ChangelogOverscroll::hint_ms * ++synthetic_steps_;
  const auto result = overscroll_.step(direction, clock, can_go, at_edge, std::abs(delta));
  scroll_.wheel(ui_, delta);
  if (result.go != 0) {
    ignore_until_ = now + switch_cooldown_ms;
    show(static_cast<std::size_t>(static_cast<int>(index_) + result.go), result.go < 0);
    return true;
  }
  show_hint(result.hint);
  idle_at_ = overscroll_.counting() ? now + idle_reset_ms : 0;
  return true;
}

bool ChangelogView::escape() {
  if (!open_) return false;
  close();
  return true;
}

Cursor ChangelogView::cursor(int id) const {
  if (id == Text) return Cursor::IBeam;
  return id == Close ? Cursor::Hand : Cursor::Arrow;
}

std::wstring_view ChangelogView::text_of(std::size_t line) const {
  return line < lines_.size() ? std::wstring_view(lines_[line].text) : std::wstring_view{};
}

std::wstring ChangelogView::selected_text() const {
  return selection_.text([this](std::size_t n) { return text_of(n); });
}

TextPoint ChangelogView::point_at(float x, float y) const {
  if (lines_.empty()) return {};
  const float ly = y - body_.y + scroll_.drawn();
  // The last line that starts above the point; above them all, the start of the first.
  std::size_t i = lines_.size();
  while (i > 0 && lines_[i - 1].y > ly) --i;
  if (i == 0) return {0, 0};
  --i;
  const Line& line = lines_[i];
  if (!line.layout) return {i, 0};
  BOOL trailing = FALSE, inside = FALSE;
  DWRITE_HIT_TEST_METRICS m{};
  line.layout->HitTestPoint(x - body_.x - line.x, ly - line.y, &trailing, &inside, &m);
  return {i, std::min<std::size_t>(m.textPosition + (trailing ? m.length : 0), line.text.size())};
}

void ChangelogView::scroll_while_selecting() {
  if (!selecting_) return;
  const float past = drag_y_ < body_.y ? drag_y_ - body_.y : drag_y_ > body_.bottom() ? drag_y_ - body_.bottom() : 0;
  if (past == 0) {
    scrolled_at_ = 0;
    return;
  }
  const float speed = std::clamp(std::abs(past) * 10, 60.0f, 2400.0f);
  const unsigned long long now = ui_.now_ms;
  const float elapsed = scrolled_at_ == 0 ? 16.0f : static_cast<float>(std::min<unsigned long long>(now - scrolled_at_, 50));
  scrolled_at_ = now;
  scroll_.jump_to(scroll_.offset + (past < 0 ? -speed : speed) * elapsed / 1000);
  scroll_.clamp();
  ui_.more_frames = true;
}

std::vector<MenuItem> ChangelogView::context_menu(int id, float x, float y) {
  if (id == Text) {
    const TextPoint at = point_at(x, y);
    if (selection_.empty() || at < selection_.start() || selection_.end() < at) selection_.clear();
    ui_.selection_owner = this;
  } else if (id != -1) {
    return {};
  }
  return {{MenuCommand::Copy, !selection_.empty()}, {MenuCommand::SelectAll, !lines_.empty()}};
}

void ChangelogView::menu_command(MenuCommand command) {
  ui_.selection_owner = this;
  key(command == MenuCommand::Copy ? 'C' : 'A', true, false);
}

bool ChangelogView::key(unsigned vk, bool ctrl, bool shift) {
  if (!ctrl || shift) return false;
  if (vk == 'C' || vk == VK_INSERT) {
    if (!selection_.empty()) ui_.copy(selected_text());
    return true;
  }
  if (vk == 'A' && !lines_.empty()) {
    selection_.select({0, 0}, {lines_.size() - 1, lines_.back().text.size()});
    return true;
  }
  return false;
}

bool ChangelogView::animating() const { return ui_.animations && open_ && was_animating_; }

bool ChangelogView::tick(unsigned long long now_ms) {
  bool changed = false;
  if (idle_at_ != 0 && now_ms >= idle_at_) {
    idle_at_ = 0;
    overscroll_.reset();
    show_hint(0);
    changed = true;
  }
  if (was_animating_) {
    was_animating_ = false;  // its screen asks for its own frames from the one this brings
    changed = true;
  }
  return changed;
}

unsigned long long ChangelogView::wake_at() const { return open_ ? idle_at_ : 0; }

}  // namespace crpp::ui
