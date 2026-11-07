#include "crpp/ui/changelog_view.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/luna.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

constexpr float card_w = 520, card_h = 480;   // .modal-card.changelog, .cl-scroll
constexpr float head_top = 22, head_bottom = 14, head_gap = 2, body_bottom = 24;
constexpr float li_indent = 20, li_margin = 4, li_line = 24.8f, heading_top = 14, heading_bottom = 6;
constexpr float hint_fade_ms = 180;                   // .cl-hint transition
constexpr unsigned long long idle_reset_ms = 1800;    // IDLE_RESET
constexpr unsigned long long switch_cooldown_ms = 500;  // SWITCH_COOLDOWN

const TextStyle tagline_style{.size = 28, .weight = 800, .line_height = 36, .wrap = true};
const TextStyle date_style{.size = 17, .line_height = 24, .wrap = true};
const TextStyle version_style{.size = 14, .line_height = 20};
const TextStyle heading_style{.size = 18, .weight = 800, .line_height = 26, .wrap = true};
const TextStyle item_style{.size = 16, .line_height = li_line, .wrap = true};

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
  opened_at_ = clock_ms();
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
  tagline_.reset();
  date_.reset();
  version_.reset();
  built_ = false;
  idle_at_ = 0;
  hint_ = hint_shown_ = 0;
  overscroll_.reset();
}

void ChangelogView::prev() {
  if (index_ > 0) show(index_ - 1, true);
}

void ChangelogView::next() {
  if (index_ + 1 < entries_.size()) show(index_ + 1, false);
}

void ChangelogView::show(std::size_t index, bool at_bottom) {
  index_ = index;
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
  hint_changed_at_ = clock_ms();
  was_animating_ = true;
}

void ChangelogView::build(float width) {
  built_ = true;
  built_width_ = width;
  lines_.clear();
  tagline_.reset();
  date_.reset();
  version_.reset();
  head_h_ = 0;
  body_h_ = 0;
  if (index_ >= entries_.size()) return;
  const ChangelogEntry& entry = entries_[index_];
  const Gfx& gfx = ui_.gfx;
  const bool plain = !ui_.skin().moves();
  const float pad_x = plain ? 12 : 28, head_right = plain ? 12 : 62;
  first_line_ = plain ? gfx.classic_size() + 4 : li_line;

  // .cl-head: a column with a gap, each line present only if it has text.
  const float head_w = width - pad_x - head_right;
  float h = 0;
  const auto add_head = [&](ComPtr<IDWriteTextLayout>& slot, const std::string& text, const TextStyle& style) {
    if (text.empty()) return;
    slot = gfx.layout(widen(text), style, head_w);
    h += (h > 0 ? head_gap : 0) + layout_height(slot.get());
  };
  add_head(tagline_, entry.tagline, tagline_style);
  add_head(date_, entry.date, date_style);
  add_head(version_, entry.version, version_style);
  head_h_ = h > 0 ? head_top + h + head_bottom : 0;
  // The first thing has no margin above it, as .cl-section:first-child h3.
  lay_out_body({pad_x, li_indent, li_margin, heading_top, heading_bottom, body_bottom, head_h_ > 0 ? 0 : head_top, heading_style, item_style}, width);
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
      y += layout_height(line.layout.get()) + look.heading_bottom;
    } else {
      const float indent = block.kind == update::NoteKind::Bullet ? look.indent * static_cast<float>(block.indent + 1) : 0;
      y += look.margin;
      line.x = look.pad_x + indent;
      line.y = y;
      line.layout = runs_layout(gfx, block.runs, look.item, width - 2 * look.pad_x - indent);
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

void ChangelogView::paint_body(Painter& p, Rect body, const TextStyle& item, float marker_h, Color ink) {
  // The body scrolls under the head, which stays (.cl-head is sticky).
  p.rt()->PushAxisAlignedClip(D2D1::RectF(body.x, body.y, body.right(), body.bottom()), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  const bool fade = push_edge_fade(ui_, p, body, scroll_.fade_top(), scroll_.fade_bottom());
  for (const Line& line : lines_) {
    const float y = body.y + line.y - scroll_.drawn();
    if (line.bullet_x >= 0) p.fill_ellipse(body.x + line.bullet_x, y + first_line_ / 2, 2.5f, 2.5f, ink);
    if (!line.marker.empty()) {
      p.text(line.marker, {body.x + line.marker_x - 4, y, li_indent, marker_h}, item, ink);
    }
    if (line.layout) p.draw_layout(line.layout.get(), body.x + line.x, y, ink);
  }
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
      v_.paint_body(p_, rect_of(box), item, item.line_height, values.colors[0]);
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
    return;
  }
  const Theme& t = ui_.theme;
  // In a window of its own: all of it, with a scrollbar.
  const bool plain = ui_.window != 0;
  const float w = plain ? ui_.content.w : std::min(card_w, ui_.width - 40);
  const float h = plain ? ui_.content.h : std::min(card_h, ui_.height - 60);
  const float pad_x = plain ? 12 : 28;
  const float text_w = plain ? w - luna::scrollbar_w : w;
  if (!built_ || built_width_ != text_w) build(text_w);

  const float shown = transition(ui_, opened_at_, modal_pop_ms);
  const Rect card = paint_modal(ui_, this, p, w, h, shown, Backdrop, Card);
  // --cl-bg: color-mix(in srgb, var(--hl) 3%, var(--surface))
  const Color background = mix(t.hl, t.surface, 3);
  if (ui_.skin().moves()) p.fill_round(card, 14, with_alpha(background, shown));
  const Color ink = with_alpha(t.hl_text, shown);

  const float view_h = card.h - head_h_;
  scroll_.view_h = view_h;
  scroll_body();

  if (entries_.empty()) {
    p.text(tr().changelog_failed, {card.x + pad_x, card.y + (card.h - 27) / 2, card.w - 2 * pad_x, 27},
           {.size = 18, .weight = 700, .line_height = 27, .align = Align::Center}, ink);
  }

  const Rect body{card.x, card.y + head_h_, card.w, view_h};
  paint_body(p, body, item_style, li_line, ink);

  float y = card.y + head_top;
  const auto paint_head = [&](const ComPtr<IDWriteTextLayout>& layout, Color color) {
    if (!layout) return;
    p.draw_layout(layout.get(), card.x + pad_x, y, color);
    y += layout_height(layout.get()) + head_gap;
  };
  paint_head(tagline_, ink);
  paint_head(date_, ink);
  paint_head(version_, with_alpha(t.hl_text_soft, shown));

  if (plain && scroll_.max_offset() > 0) {
    const Rect bar{card.right() - luna::scrollbar_w, body.y, luna::scrollbar_w, body.h};
    const auto parts = luna::scrollbar_parts(bar, scroll_.view_h, scroll_.content_h, scroll_.offset);
    bar_track_ = parts.track;
    luna::scrollbar(p, t, parts, ui_.hot(this, BarThumb), false, false);
    ui_.add_hit_window(this, bar, BarTrack);
    ui_.add_hit_window(this, parts.thumb, BarThumb);
  }

  // .cl-close: 36 across, 15 from the top and 16 from the right. A window has its own.
  if (!plain) {
    const Rect close{card.right() - 16 - 36, card.y + 15, 36, 36};
    ui_.skin().close_button(p, t, close, ui_.hot(this, Close), shown);
    ui_.add_hit_window(this, close, Close);
  }

  if (hint_shown_ != 0) {
    const float progress = transition(ui_, hint_changed_at_, hint_fade_ms);
    const float a = hint_ != 0 ? progress : 1 - progress;
    if (a > 0) {
      const std::string_view text = hint_shown_ < 0 ? tr().changelog_newer_hint
                                                    : tr().changelog_older_hint;
      const TextStyle style{.size = 15, .weight = 700, .line_height = 22, .align = Align::Center, .wrap = true};
      DWRITE_TEXT_METRICS m{};
      if (const auto layout = ui_.gfx.layout(widen(text), style, card.w)) layout->GetMetrics(&m);
      const float bubble_w = m.widthIncludingTrailingWhitespace + 30, bubble_h = m.height + 10;
      const float settled = hint_shown_ < 0 ? card.y + 14 : card.bottom() - 14 - bubble_h;
      const Rect bubble{card.x + (card.w - bubble_w) / 2, settled + 4 * (1 - a), bubble_w, bubble_h};
      const Color written = ui_.skin().bubble(p, t, bubble, 11, t.hl, t.on_accent, a);
      p.text(text, {bubble.x, bubble.y + 5, bubble.w, m.height}, style, written);
    }
  }
}

void ChangelogView::click(int id, float, float y, bool) {
  if (id == Close || id == Backdrop) {
    close();
  } else if (repeats(id)) {
    scroll_.step_towards(bar_track_, y);
  } else if (id == BarThumb) {
    ui_.capture = this;
    grab_y_ = y;
    grab_offset_ = scroll_.offset;
  }
}

void ChangelogView::drag(float, float y) {
  const float travel = std::max(1.0f, bar_track_.h - scroll_.thumb(bar_track_).h);
  scroll_.jump_to(grab_offset_ + (y - grab_y_) * scroll_.max_offset() / travel);
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

Cursor ChangelogView::cursor(int id) const { return id == Close ? Cursor::Hand : Cursor::Arrow; }

bool ChangelogView::animating() const { return ui_.animations && open_ && was_animating_; }

bool ChangelogView::tick(unsigned long long now_ms) {
  bool changed = false;
  if (idle_at_ != 0 && now_ms >= idle_at_) {
    idle_at_ = 0;
    overscroll_.reset();
    show_hint(0);
    changed = true;
  }
  if (was_animating_ && ui_.pack_screen("changelog")) {
    was_animating_ = false;  // its screen asks for its own frames from the one this brings
    changed = true;
  } else if (was_animating_) {
    if (static_cast<float>(now_ms - opened_at_) >= modal_pop_ms && static_cast<float>(now_ms - hint_changed_at_) >= hint_fade_ms) {
      was_animating_ = false;
      if (hint_ == 0) hint_shown_ = 0;
    }
    changed = true;
  }
  return changed;
}

unsigned long long ChangelogView::wake_at() const { return open_ ? idle_at_ : 0; }

}  // namespace crpp::ui
