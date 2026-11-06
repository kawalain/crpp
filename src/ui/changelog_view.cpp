#include "crpp/ui/changelog_view.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>

#include "crpp/ui/controls.hpp"

namespace crpp::ui {

namespace {

constexpr const char* close_path = "M12 12 24 24M24 12 12 24";
constexpr float card_w = 520, card_h = 480;   // .modal-card.changelog, .cl-scroll
constexpr float pad_x = 28, head_top = 22, head_bottom = 14, head_right = 62, head_gap = 2, body_bottom = 24;
constexpr float li_indent = 20, li_margin = 4, li_line = 24.8f, heading_top = 14, heading_bottom = 6;
constexpr float scroll_step = 80;
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

}  // namespace

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
  offset_ = 0;
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

  // .cl-body. The first thing has no margin above it, as .cl-section:first-child h3.
  float y = head_h_ > 0 ? 0 : head_top;
  bool first = true;
  for (const update::NoteBlock& block : entry.blocks) {
    Line line;
    if (block.kind == update::NoteKind::Heading) {
      y += first ? 0 : heading_top;
      line.x = pad_x;
      line.y = y;
      line.layout = runs_layout(gfx, block.runs, heading_style, width - 2 * pad_x);
      y += layout_height(line.layout.get()) + heading_bottom;
    } else {
      const float indent = block.kind == update::NoteKind::Bullet ? li_indent * static_cast<float>(block.indent + 1) : 0;
      y += li_margin;
      line.x = pad_x + indent;
      line.y = y;
      line.layout = runs_layout(gfx, block.runs, item_style, width - 2 * pad_x - indent);
      if (block.kind == update::NoteKind::Bullet) {
        if (block.marker.empty()) {
          line.bullet_x = line.x - 11;
          line.bullet_y = y + li_line / 2;
        } else {
          line.marker = widen(block.marker);
          line.marker_x = line.x - li_indent;
        }
      }
      y += layout_height(line.layout.get()) + li_margin;
    }
    lines_.push_back(std::move(line));
    first = false;
  }
  body_h_ = y + body_bottom;
}

void ChangelogView::paint(Painter& p) {
  if (!open_) return;
  const Theme& t = ui_.theme;
  const float w = std::min(card_w, ui_.width - 40);
  const float h = std::min(card_h, ui_.height - 60);
  if (!built_ || built_width_ != w) build(w);

  const float shown = transition(ui_, opened_at_, modal_pop_ms);
  const Rect card = paint_modal(ui_, this, p, w, h, shown, Backdrop, Card);
  // --cl-bg: color-mix(in srgb, var(--hl) 3%, var(--surface))
  const Color background = mix(t.hl, t.surface, 3);
  p.fill_round(card, 14, with_alpha(background, shown));
  const Color ink = with_alpha(t.hl_text, shown);

  const float view_h = card.h - head_h_;
  max_offset_ = std::max(0.0f, body_h_ - view_h);
  if (start_at_bottom_) {
    offset_ = max_offset_;
    start_at_bottom_ = false;
  }
  offset_ = std::clamp(offset_, 0.0f, max_offset_);

  if (entries_.empty()) {
    p.text("변경사항을 불러오지 못했어요.", {card.x + pad_x, card.y + (card.h - 27) / 2, card.w - 2 * pad_x, 27},
           {.size = 18, .weight = 700, .line_height = 27, .align = Align::Center}, ink);
  }

  // The body scrolls under the head, which stays (.cl-head is sticky).
  const Rect body{card.x, card.y + head_h_, card.w, view_h};
  p.rt()->PushAxisAlignedClip(D2D1::RectF(body.x, body.y, body.right(), body.bottom()), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  for (const Line& line : lines_) {
    const float y = body.y + line.y - offset_;
    if (line.bullet_x >= 0) p.fill_ellipse(card.x + line.bullet_x, y - line.y + line.bullet_y, 2.5f, 2.5f, ink);
    if (!line.marker.empty()) {
      p.text(line.marker, {card.x + line.marker_x - 4, y, li_indent, li_line}, item_style, ink);
    }
    if (line.layout) p.draw_layout(line.layout.get(), card.x + line.x, y, ink);
  }
  p.rt()->PopAxisAlignedClip();

  float y = card.y + head_top;
  const auto paint_head = [&](const ComPtr<IDWriteTextLayout>& layout, Color color) {
    if (!layout) return;
    p.draw_layout(layout.get(), card.x + pad_x, y, color);
    y += layout_height(layout.get()) + head_gap;
  };
  paint_head(tagline_, ink);
  paint_head(date_, ink);
  paint_head(version_, with_alpha(t.hl_text_soft, shown));

  // .cl-close: 36 across, 15 from the top and 16 from the right.
  const Rect close{card.right() - 16 - 36, card.y + 15, 36, 36};
  if (!close_icon_) close_icon_ = ui_.gfx.parse_path(close_path);
  if (ui_.hot(this, Close)) p.fill_ellipse(close.x + 18, close.y + 18, 18, 18, with_alpha(t.hl_pale2, shown));
  if (close_icon_) {
    p.push(D2D1::Matrix3x2F::Translation(close.x, close.y));
    p.stroke_geometry(close_icon_.get(), with_alpha(t.hl, shown), 4.4f, false);
    p.pop();
  }
  ui_.add_hit_window(this, close, Close);

  if (hint_shown_ != 0) {
    const float progress = transition(ui_, hint_changed_at_, hint_fade_ms);
    const float a = hint_ != 0 ? progress : 1 - progress;
    if (a > 0) {
      const std::string_view text = hint_shown_ < 0 ? "위로 더 올리면\n최신 변경사항을 표시합니다."
                                                    : "아래로 더 내리면\n이전 변경사항을 표시합니다.";
      const TextStyle style{.size = 15, .weight = 700, .line_height = 22, .align = Align::Center, .wrap = true};
      DWRITE_TEXT_METRICS m{};
      if (const auto layout = ui_.gfx.layout(widen(text), style, card.w)) layout->GetMetrics(&m);
      const float bubble_w = m.widthIncludingTrailingWhitespace + 30, bubble_h = m.height + 10;
      const float settled = hint_shown_ < 0 ? card.y + 14 : card.bottom() - 14 - bubble_h;
      const Rect bubble{card.x + (card.w - bubble_w) / 2, settled + 4 * (1 - a), bubble_w, bubble_h};
      p.fill_round(bubble, 11, with_alpha(t.hl, a));
      p.text(text, {bubble.x, bubble.y + 5, bubble.w, m.height}, style, with_alpha(t.on_accent, a));
    }
  }
}

void ChangelogView::click(int id, float, float, bool) {
  if (id == Close || id == Backdrop) close();
}

bool ChangelogView::wheel(float, float, float delta) {
  if (!open_) return false;
  const auto now = clock_ms();
  const int direction = delta < 0 ? 1 : delta > 0 ? -1 : 0;
  if (direction == 0 || (ui_.animations && now < ignore_until_)) return true;
  const bool can_go = direction > 0 ? index_ + 1 < entries_.size() : index_ > 0;
  const bool at_edge = direction > 0 ? offset_ >= max_offset_ - 1 : offset_ <= 1;
  // With animations off (tests, --shot) the wait for the hint has already passed.
  const auto clock = ui_.animations ? now : now + ChangelogOverscroll::hint_ms * ++synthetic_steps_;
  const auto result = overscroll_.step(direction, clock, can_go, at_edge, std::abs(delta));
  offset_ = std::clamp(offset_ - delta * scroll_step, 0.0f, max_offset_);
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
  if (was_animating_) {
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
