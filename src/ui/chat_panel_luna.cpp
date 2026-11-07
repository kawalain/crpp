// The chat in the Luna Skin: "name says:" above an indented message, in a white list, with the
// message being written under it. Shares its log, limiter and callbacks with the Cloud layout.
#include <algorithm>
#include <cmath>

#include "crpp/ui/chat_panel.hpp"
#include "crpp/ui/luna.hpp"

namespace crpp::ui {

namespace {
constexpr float indent = 14, list_pad = 7, row_gap = 7;
}  // namespace

// The chat's size setting is a font size here, so the letters stay crisp.
TextStyle ChatPanel::luna_body() const {
  const float size = std::round(ui_.gfx.classic_size() * zoom_);
  return {.size = size, .line_height = size + 4, .wrap = true, .exact = true};
}

TextStyle ChatPanel::luna_input() const {
  const float size = ui_.gfx.classic_size();
  return {.size = size, .line_height = size + 4, .wrap = true, .exact = true};
}

void ChatPanel::append_row_luna(std::size_t index, float list_w) {
  const ChatEntry& e = log_[index];
  const bool cont = chat_grouped_with_previous(index);
  const TextStyle body = luna_body();
  Row row;
  row.head = !cont;
  if (cont) {
    next_y_ -= row_gap - 2;
  } else {
    row.y_head = next_y_;
    next_y_ += body.line_height + 1;
  }
  const std::wstring text = widen(e.text);
  DWRITE_TEXT_METRICS m{};
  if (const auto layout = ui_.gfx.layout(text, body, list_w - indent, 100000)) layout->GetMetrics(&m);
  row.y_text = next_y_;
  row.h_text = m.height;
  row.links = find_links(text);
  next_y_ += m.height + row_gap;
  rows_.push_back(std::move(row));
}

void ChatPanel::paint_luna(Painter& p, Rect panel) {
  const Theme& t = ui_.theme;
  const LunaColors& l = t.luna;
  const TextStyle input_style = luna_input();
  const float send_w = luna::button_w;
  const float field_w = panel.w - send_w - 6;
  input_text_w_ = field_w - 8;
  const float input_h = std::clamp(std::ceil(input_.measure_height(ui_.gfx, input_style, field_w - 8)) + 8, 52.0f, 100.0f);
  const Rect list{panel.x, panel.y, panel.w, panel.h - input_h - 6};
  area_ = list;
  luna::sunken(p, t, list);
  ui_.add_hit_window(this, list, Area);

  // --- the messages ---
  const Rect inner{list.x + 1, list.y + 1, list.w - 2, list.h - 2};
  const float text_w_full = inner.w - 2 * list_pad - luna::scrollbar_w;
  if (fresh_ && !log_.empty()) {
    first_ = log_.size() > chat_page ? log_.size() - chat_page : 0;
    fresh_ = false;
    dirty_ = true;
  }
  const float size_key = text_w_full + zoom_ * 1000;  // another chat size measures again too
  if (dirty_ || size_key != rows_w_) {
    rows_.clear();
    next_y_ = list_pad;
    rows_w_ = size_key;
    dirty_ = false;
    kept_from_ = kept_to_ = 0;
  }
  const float content_before = content_h_;
  const std::size_t shown_before = rows_.size();
  for (std::size_t i = first_ + rows_.size(); i < log_.size(); ++i) append_row_luna(i, text_w_full);
  content_h_ = next_y_ - row_gap + list_pad;
  scroll_.view_h = inner.h;
  scroll_.content_h = content_h_;
  const float max_scroll = scroll_.max_offset();
  if (older_pending_ && !stick_ && rows_.size() > shown_before) {
    scroll_.offset += content_h_ - content_before;
    scroll_.target += content_h_ - content_before;
  }
  older_pending_ = false;
  if (stick_) scroll_.jump_to(max_scroll);
  scroll_.clamp();
  scroll_.advance(ui_);

  const TextStyle body = luna_body();
  const TextStyle head{.size = body.size, .line_height = body.line_height, .ellipsis = true, .exact = true};
  const TextStyle time_style{.size = body.size, .line_height = body.line_height, .align = Align::Right, .exact = true};
  const float x = inner.x + list_pad, top = inner.y - scroll_.drawn();
  HoverTip::Spec tip;
  bool show_tip = false;
  p.rt()->PushAxisAlignedClip(D2D1::RectF(inner.x, inner.y, inner.right() - luna::scrollbar_w, inner.bottom()), D2D1_ANTIALIAS_MODE_ALIASED);
  ui_.push_hit_clip({inner.x, inner.y, inner.w - luna::scrollbar_w, inner.h});
  std::size_t in_view_from = rows_.size(), in_view_to = 0;
  for (std::size_t r = 0; r < rows_.size(); ++r) {
    Row& row = rows_[r];
    const float row_top = top + (row.head ? row.y_head : row.y_text);
    if (top + row.y_text + row.h_text <= inner.y) continue;
    if (row_top >= inner.bottom()) break;
    in_view_from = std::min(in_view_from, r);
    in_view_to = r + 1;
    const std::size_t index = first_ + r;
    const ChatEntry& e = log_[index];
    if (row.head) {
      if (!row.time) {
        row.time = ui_.gfx.layout(widen(chat_clock_text(e.time_ms, use_24h_)), time_style, text_w_full, head.line_height);
        DWRITE_TEXT_METRICS m{};
        if (row.time) row.time->GetMetrics(&m);
        row.time_w = m.widthIncludingTrailingWhitespace;
      }
      if (row.time) p.draw_layout(row.time.get(), x, top + row.y_head, l.ink_soft);
      if (!row.nick) {
        const std::string name = e.nick.empty() ? std::string(tr().guest) : e.nick;
        row.nick = ui_.gfx.layout(widen(locale::fill(tr().chat_says, {name})), head, std::max(20.0f, text_w_full - row.time_w - 8), head.line_height);
      }
      if (row.nick) p.draw_layout(row.nick.get(), x, top + row.y_head, l.ink_soft);
    }
    if (!row.text) row.text = ui_.gfx.layout(widen(e.text), body, text_w_full - indent, 100000);
    if (!row.text) continue;
    IDWriteTextLayout* const layout = row.text.get();
    const float text_x = x + indent, text_y = top + row.y_text;
    for (std::size_t n = 0; n < row.links.size() && n < links_per_entry; ++n) {
      const LinkSpan link = row.links[n];
      const int id = LinkBase + static_cast<int>(dropped_ + index) * links_per_entry + static_cast<int>(n);
      DWRITE_HIT_TEST_METRICS boxes[8];
      UINT32 count = 0;
      if (FAILED(layout->HitTestTextRange(link.start, link.length, 0, 0, boxes, 8, &count))) continue;
      for (UINT32 b = 0; b < count; ++b) {
        const Rect box{text_x + boxes[b].left, text_y + boxes[b].top, boxes[b].width, boxes[b].height};
        ui_.add_hit_window(this, box, id);
        if (b == 0) tip.anchor = box;
      }
      layout->SetUnderline(true, {link.start, link.length});
      if (ui_.hot(this, id)) {
        tip.text = tr().chat_link_tip;
        tip.gap = 2;
        tip.slide = 0;
        show_tip = true;
      }
    }
    p.draw_layout(layout, text_x, text_y, l.ink);
  }
  for (std::size_t r = kept_from_; r < kept_to_ && r < rows_.size(); ++r) {
    if (r >= in_view_from && r < in_view_to) continue;
    rows_[r].text.reset();
    rows_[r].nick.reset();
    rows_[r].time.reset();
  }
  kept_from_ = in_view_from;
  kept_to_ = in_view_to;
  ui_.pop_hit_clip();
  p.rt()->PopAxisAlignedClip();

  const Rect bar{inner.right() - luna::scrollbar_w, inner.y, luna::scrollbar_w, inner.h};
  const auto parts = luna::scrollbar_parts(bar, scroll_.view_h, scroll_.content_h, scroll_.offset);
  bar_track_ = parts.track;
  luna::scrollbar(p, t, parts, ui_.hot(this, BarThumb), ui_.hot(this, BarUp), ui_.hot(this, BarDown));
  ui_.add_hit_window(this, bar, BarTrack);
  ui_.add_hit_window(this, parts.up, BarUp);
  ui_.add_hit_window(this, parts.down, BarDown);
  ui_.add_hit_window(this, parts.thumb, BarThumb);

  // --- what is being written ---
  const Rect field{panel.x, panel.bottom() - input_h, field_w, input_h};
  luna::field(p, t, field);
  const bool focused = ui_.focus == &input_;
  const Rect box{field.x + 4, field.y + 4, field.w - 8, field.h - 8};
  p.rt()->PushAxisAlignedClip(D2D1::RectF(field.x + 1, field.y + 1, field.right() - 1, field.bottom() - 1), D2D1_ANTIALIAS_MODE_ALIASED);
  const Rect caret = input_.paint(p, box, input_style, l.ink, widen(tr().chat_placeholder), input_style, l.ink_soft, l.ink, with_alpha(l.select, 0.4f), focused, ui_.caret_on);
  p.rt()->PopAxisAlignedClip();
  if (focused) ui_.caret = caret;
  ui_.add_hit_window(this, field, Input);

  const Rect send{panel.right() - send_w, field.y, send_w, input_h};
  const bool off = input_.text().empty() || limiter_.cooling();
  luna::button(p, t, send, tr().chat_send, {.hot = !off && ui_.hot(this, Send), .pressed = !off && ui_.pressed(this, Send), .off = off});
  ui_.add_hit_window(this, send, Send);

  // Too fast: said in a small box, over the button.
  if (limiter_.calm()) {
    const std::string text = limiter_.calm_text();
    DWRITE_TEXT_METRICS m{};
    const TextStyle style = luna::body(p, 16, Align::Center);
    if (const auto layout = ui_.gfx.layout(widen(text), style, 400)) layout->GetMetrics(&m);
    const float w = std::ceil(m.widthIncludingTrailingWhitespace) + 10;
    const Rect tip_box{std::max(panel.x, send.right() - w), send.y - 22, w, 18};
    const Color ink = ui_.skin().bubble(p, t, tip_box, 0, {}, {}, 1);
    p.text(text, tip_box, style, ink);
  }

  link_tip_.paint(ui_, p, show_tip ? &tip : nullptr);
}

}  // namespace crpp::ui
