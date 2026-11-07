// The chat drawn from a Skin Pack, and its plain list of messages: "name says:" above an indented
// message. Rows keep their layouts while in view, so they stay the panel's to draw.
#include <algorithm>
#include <cmath>

#include "crpp/skin/draw.hpp"
#include "crpp/ui/chat_panel.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

enum TextId : int { InputText, CalmText };
enum NumberId : int { Zoom, View, Content, Offset, SendOff, Calm };
enum NoteId : int { AreaNote, BarTrackNote };

constexpr skin::Name text_names[] = {{"input_text", InputText}, {"calm_text", CalmText}};
constexpr skin::Name number_names[] = {{"zoom", Zoom}, {"view", View}, {"content", Content}, {"offset", Offset}, {"send_off", SendOff}, {"calm", Calm}};
constexpr skin::Name field_names[] = {{"input", 0}};
constexpr skin::Name slot_names[] = {{"messages", 0}};
constexpr skin::Name note_names[] = {{"area", AreaNote}, {"bar_track", BarTrackNote}};
constexpr skin::Name hit_names[] = {{"input", ChatPanel::Input}, {"send", ChatPanel::Send}, {"area", ChatPanel::Area}, {"bar_up", ChatPanel::BarUp}, {"bar_down", ChatPanel::BarDown}, {"bar_track", ChatPanel::BarTrack}, {"bar_thumb", ChatPanel::BarThumb}};

}  // namespace

skin::ScreenSchema ChatPanel::schema() {
  return {.screen = "chat", .texts = text_names, .numbers = number_names, .fields = field_names, .hits = hit_names, .slots = slot_names, .notes = note_names};
}

void ChatPanel::paint_pack(Painter& p, const skin::Screen& screen, Rect panel) {
  class Shown final : public skin::Source {
   public:
    Shown(ChatPanel& chat, Painter& painter) : s_(chat), p_(painter) {
      const std::wstring text = s_.input_.shown();
      input_text_ = text.empty() ? std::string(" ") : narrow(text);
      if (s_.limiter_.calm()) calm_text_ = s_.limiter_.calm_text();
    }

    std::string_view text(int id, int) override { return id == InputText ? input_text_ : calm_text_; }

    float number(int id, int) override {
      switch (id) {
        case Zoom: return s_.zoom_;
        case View: return s_.scroll_.view_h;
        case Content: return s_.scroll_.content_h;
        case Offset: return s_.scroll_.offset;
        case SendOff: return s_.input_.text().empty() || s_.limiter_.cooling() ? 1.0f : 0.0f;
        case Calm: return s_.limiter_.calm() ? 1.0f : 0.0f;
        default: return 0;
      }
    }

    void field(int, int, skin::Box box, const skin::FieldLook& look) override {
      const TextStyle style = text_style_of(look.text);
      s_.pack_input_style_ = style;
      s_.input_text_w_ = box.w;
      const bool focused = s_.ui_.focus == &s_.input_;
      const Rect caret = s_.input_.paint(p_, rect_of(box), style, look.color, widen(look.placeholder), style, look.placeholder_color, look.caret, look.selection, focused, s_.ui_.caret_on);
      if (focused) s_.ui_.caret = caret;
    }

    void slot(int, int, skin::Box box, const skin::SlotValues& values) override {
      TextStyle body = text_style_of(values.styles[0]);
      body.size = values.numbers[4];
      body.line_height = values.numbers[5];
      s_.messages_ = {.indent = values.numbers[0], .pad = values.numbers[1], .gap = values.numbers[2], .bar_w = values.numbers[3], .body = body, .ink = values.colors[0], .soft = values.colors[1]};
      s_.paint_messages(p_, rect_of(box));
    }

    void note(int id, int, skin::Box box) override { (id == AreaNote ? s_.area_ : s_.bar_track_) = rect_of(box); }

   private:
    ChatPanel& s_;
    Painter& p_;
    std::string input_text_, calm_text_;
  } shown(*this, p);

  SkinCanvas canvas(ui_, p, this);
  skin::draw(*ui_.pack, screen, ui_.palette, {panel.x, panel.y, panel.w, panel.h}, canvas, shown);
  link_tip_.paint(ui_, p, show_link_tip_ ? &link_tip_spec_ : nullptr);
}

void ChatPanel::append_message(std::size_t index, float list_w) {
  const ChatEntry& e = log_[index];
  const bool cont = chat_grouped_with_previous(index);
  Row row;
  row.head = !cont;
  if (cont) {
    next_y_ -= messages_.gap - 2;
  } else {
    row.y_head = next_y_;
    next_y_ += messages_.body.line_height + 1;
  }
  const std::wstring text = widen(e.text);
  DWRITE_TEXT_METRICS m{};
  if (const auto layout = ui_.gfx.layout(text, messages_.body, list_w - messages_.indent, 100000)) layout->GetMetrics(&m);
  row.y_text = next_y_;
  row.h_text = m.height;
  row.links = find_links(text);
  next_y_ += m.height + messages_.gap;
  rows_.push_back(std::move(row));
}

void ChatPanel::paint_messages(Painter& p, Rect inner) {
  const MessageLook& look = messages_;
  const float text_w_full = inner.w - 2 * look.pad - look.bar_w;
  if (fresh_ && !log_.empty()) {
    first_ = log_.size() > chat_page ? log_.size() - chat_page : 0;
    fresh_ = false;
    dirty_ = true;
  }
  const float size_key = text_w_full + zoom_ * 1000;  // another chat size measures again too
  if (dirty_ || size_key != rows_w_) {
    rows_.clear();
    next_y_ = look.pad;
    rows_w_ = size_key;
    dirty_ = false;
    kept_from_ = kept_to_ = 0;
  }
  const float content_before = content_h_;
  const std::size_t shown_before = rows_.size();
  for (std::size_t i = first_ + rows_.size(); i < log_.size(); ++i) append_message(i, text_w_full);
  content_h_ = next_y_ - look.gap + look.pad;
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

  const TextStyle& body = look.body;
  const TextStyle head{.size = body.size, .line_height = body.line_height, .ellipsis = true, .exact = true};
  const TextStyle time_style{.size = body.size, .line_height = body.line_height, .align = Align::Right, .exact = true};
  const float x = inner.x + look.pad, top = inner.y - scroll_.drawn();
  show_link_tip_ = false;
  link_tip_spec_ = {};
  p.rt()->PushAxisAlignedClip(D2D1::RectF(inner.x, inner.y, inner.right() - look.bar_w, inner.bottom()), D2D1_ANTIALIAS_MODE_ALIASED);
  ui_.push_hit_clip({inner.x, inner.y, inner.w - look.bar_w, inner.h});
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
      if (row.time) p.draw_layout(row.time.get(), x, top + row.y_head, look.soft);
      if (!row.nick) {
        const std::string name = e.nick.empty() ? std::string(tr().guest) : e.nick;
        row.nick = ui_.gfx.layout(widen(locale::fill(tr().chat_says, {name})), head, std::max(20.0f, text_w_full - row.time_w - 8), head.line_height);
      }
      if (row.nick) p.draw_layout(row.nick.get(), x, top + row.y_head, look.soft);
    }
    if (!row.text) row.text = ui_.gfx.layout(widen(e.text), body, text_w_full - look.indent, 100000);
    if (!row.text) continue;
    IDWriteTextLayout* const layout = row.text.get();
    const float text_x = x + look.indent, text_y = top + row.y_text;
    for (std::size_t n = 0; n < row.links.size() && n < links_per_entry; ++n) {
      const LinkSpan link = row.links[n];
      const int id = LinkBase + static_cast<int>(dropped_ + index) * links_per_entry + static_cast<int>(n);
      DWRITE_HIT_TEST_METRICS boxes[8];
      UINT32 count = 0;
      if (FAILED(layout->HitTestTextRange(link.start, link.length, 0, 0, boxes, 8, &count))) continue;
      for (UINT32 b = 0; b < count; ++b) {
        const Rect box{text_x + boxes[b].left, text_y + boxes[b].top, boxes[b].width, boxes[b].height};
        ui_.add_hit_window(this, box, id);
        if (b == 0) link_tip_spec_.anchor = box;
      }
      layout->SetUnderline(true, {link.start, link.length});
      if (ui_.hot(this, id)) {
        link_tip_spec_.text = tr().chat_link_tip;
        link_tip_spec_.look = HoverTip::Look::Link;
        link_tip_spec_.gap = 2;
        link_tip_spec_.slide = 0;
        show_link_tip_ = true;
      }
    }
    p.draw_layout(layout, text_x, text_y, look.ink);
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
}

}  // namespace crpp::ui
