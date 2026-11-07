#include "crpp/ui/chat_panel.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/toasts.hpp"

namespace crpp::ui {

std::string chat_clock_text(long long ms, bool use_24h) {
  const std::time_t t = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
  localtime_s(&tm, &t);
  const std::time_t now = static_cast<std::time_t>(wall_clock_seconds());
  std::tm today{};
  localtime_s(&today, &now);
  const auto day_index = [](const std::tm& d) {
    std::tm copy = d;
    copy.tm_hour = copy.tm_min = copy.tm_sec = 0;
    return static_cast<long long>(std::mktime(&copy)) / 86400;
  };
  char clock[32];
  if (use_24h) {
    std::snprintf(clock, sizeof clock, "%02d:%02d", tm.tm_hour, tm.tm_min);
  } else {
    std::snprintf(clock, sizeof clock, "%s %d:%02d", tm.tm_hour < 12 ? "오전" : "오후",
                  tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12, tm.tm_min);
  }
  const long long days = day_index(today) - day_index(tm);
  if (days <= 0) return clock;
  if (days == 1) return std::string("어제 ") + clock;
  if (days == 2) return std::string("그저께 ") + clock;
  char date[16];
  std::snprintf(date, sizeof date, "%02d.%02d ", tm.tm_mon + 1, tm.tm_mday);
  return std::string(date) + clock;
}

namespace {

bool chat_grouped(const ChatEntry& e, const ChatEntry* prev) {
  if (!prev || prev->slot != e.slot || prev->cid != e.cid) return false;
  const long long gap = e.time_ms - prev->time_ms;
  return gap >= 0 && gap <= 5 * 60 * 1000;
}

constexpr TextStyle body_style{.size = 17, .line_height = 19, .wrap = true};

// @keyframes chat-shake, over 420 ms.
constexpr std::pair<float, float> send_shake[] = {{0.0f, 0.0f}, {0.15f, -7.0f}, {0.3f, 7.0f}, {0.45f, -5.0f},
                                                  {0.6f, 5.0f}, {0.75f, -3.0f}, {0.9f, 3.0f}, {1.0f, 0.0f}};
constexpr float send_shake_ms = 420;
constexpr float send_move_ms = 240;

// .calm-bubble: springs out of the send button, and shrinks back faster.
constexpr float calm_in_ms = 420, calm_out_ms = 140;
constexpr float arrive_ms = 300, arrive_rise = 8;
constexpr const char* calm_tail = "M0 0H32C28 10 27 22 31 34C17 27 15 18 10 12C7 6 3 3 0 0Z";

// CSS cubic-bezier(x1, y1, x2, y2) at t; y may overshoot.
float bezier_ease(float x1, float y1, float x2, float y2, float t) {
  if (t <= 0) return 0;
  if (t >= 1) return 1;
  const auto curve = [](float a, float b, float u) {
    const float v = 1 - u;
    return 3 * v * v * u * a + 3 * v * u * u * b + u * u * u;
  };
  float low = 0, high = 1;
  for (int i = 0; i < 16; ++i) {
    const float u = (low + high) / 2;
    if (curve(x1, x2, u) < t) low = u;
    else high = u;
  }
  return curve(y1, y2, (low + high) / 2);
}

}  // namespace

ChatPanel::ChatPanel(Ui& ui) : Surface(ui) {
  input_.max_length = 200;
  input_.multiline = true;
}

void ChatPanel::add(ChatEntry entry) {
  log_.push_back(std::move(entry));
  if (log_.size() > 1000) {
    log_.erase(log_.begin());
    ++dropped_;
    if (first_ > 0) --first_;
    else dirty_ = true;  // the first shown one is gone
  }
}

void ChatPanel::clear() {
  dropped_ += log_.size();
  log_.clear();
  rows_.clear();
  first_ = 0;
  fresh_ = dirty_ = true;
  scroll_ = {};
  stick_ = true;
  input_.clear();
  limiter_.reset();
  last_sent_.clear();
  shake_at_ = 0;
  calm_shown_ = false;
  calm_since_ = 0;
  multi_ = false;
  multi_since_ = 0;
}

void ChatPanel::send() {
  std::wstring text = input_.text();
  const auto blank = [](wchar_t c) { return c == L' ' || c == 0x0A; };
  while (!text.empty() && blank(text.back())) text.pop_back();
  std::size_t lead = 0;
  while (lead < text.size() && blank(text[lead])) ++lead;
  text.erase(0, lead);
  if (text.empty()) return;
  std::string message = narrow(text);
  if (!limiter_.try_send(clock_ms())) {
    // Still sent: every attempt prolongs the block on the server too, and its answer
    // keeps the two in step. The text stays in the box.
    shake();
    if (on_send) on_send(std::move(message));
    return;
  }
  last_sent_ = message;
  if (on_send && !on_send(std::move(message))) {
    if (ui_.toasts) ui_.toasts->toast("메시지를 보내지 못했어요.");
    return;
  }
  input_.clear();
}

void ChatPanel::blocked(std::uint64_t ms) {
  if (!limiter_.server_blocked(clock_ms(), ms)) return;
  shake();
  if (input_.text().empty() && !last_sent_.empty()) input_.set_text(widen(last_sent_));
}

void ChatPanel::shake() { shake_at_ = clock_ms(); }

bool ChatPanel::shaking() const {
  return ui_.animations && shake_at_ != 0 && static_cast<float>(ui_.now_ms - std::min(ui_.now_ms, shake_at_)) < send_shake_ms;
}

bool ChatPanel::calm_moving() const {
  if (!ui_.animations) return false;
  if (calm_shown_ != limiter_.calm()) return true;
  return calm_since_ != 0 && static_cast<float>(ui_.now_ms - std::min(ui_.now_ms, calm_since_)) < (calm_shown_ ? calm_in_ms : calm_out_ms);
}

float ChatPanel::multi_progress() const {
  const bool moving = ui_.animations && multi_since_ != 0;
  const float elapsed = moving ? static_cast<float>(ui_.now_ms - std::min(ui_.now_ms, multi_since_)) : send_move_ms;
  const float moved = bezier_ease(0.22f, 0.9f, 0.3f, 1, elapsed / send_move_ms);
  return multi_ ? moved : 1 - moved;
}

bool ChatPanel::send_moving() const {
  return ui_.animations && multi_since_ != 0 && static_cast<float>(ui_.now_ms - std::min(ui_.now_ms, multi_since_)) < send_move_ms;
}

bool ChatPanel::animating() const { return shaking() || calm_moving() || send_moving() || link_tip_.animating(ui_); }

bool ChatPanel::tick(unsigned long long now_ms) {
  const bool moving = animating();
  return limiter_.expire(now_ms) || moving;
}

Cursor ChatPanel::cursor(int id) const {
  if (id == Input) return Cursor::IBeam;
  return id == Area ? Cursor::Arrow : Cursor::Hand;
}

void ChatPanel::append_row(std::size_t index, float list_w, bool arriving) {
  const ChatEntry& e = log_[index];
  const bool cont = chat_grouped(e, index > first_ ? &log_[index - 1] : nullptr);
  Row row;
  row.head = !cont;
  if (cont) {
    next_y_ -= 10;
  } else {
    row.y_head = next_y_;
    next_y_ += 25 + 2;
  }
  const std::wstring text = widen(e.text);
  DWRITE_TEXT_METRICS m{};
  if (const auto layout = ui_.gfx.layout(text, body_style, list_w, 100000)) layout->GetMetrics(&m);
  row.y_text = next_y_;
  row.h_text = m.height;
  row.links = find_links(text);
  next_y_ += m.height + 14;
  if (arriving) {
    // A burst rises as one, so the list does not keep jerking under it.
    if (ui_.now_ms >= arrivals_at_ + static_cast<unsigned long long>(arrive_ms)) arrivals_at_ = ui_.now_ms;
    row.arrived_at = arrivals_at_;
  }
  rows_.push_back(std::move(row));
}

// Brings rows_ up to date; a message that arrived costs one measurement.
void ChatPanel::sync_rows(float list_w) {
  if (fresh_ && !log_.empty()) {
    first_ = log_.size() > chat_page ? log_.size() - chat_page : 0;
    fresh_ = false;
    dirty_ = true;
  }
  const bool again = dirty_ || list_w != rows_w_;
  if (again) {
    rows_.clear();
    next_y_ = 24;
    rows_w_ = list_w;
    dirty_ = false;
    kept_from_ = kept_to_ = 0;
  }
  // Only a new message at the end of what is being read arrives; a history just is.
  const bool arriving = !again && stick_ && ui_.animations;
  for (std::size_t i = first_ + rows_.size(); i < log_.size(); ++i) append_row(i, list_w, arriving);
}

void ChatPanel::paint(Painter& p, Rect panel) {
  const Theme& t = ui_.theme;
  const TextStyle input_style{.size = 17, .line_height = 22, .wrap = true};
  // .chat-panel.multi: from two lines on the button moves out and the text takes its place.
  const float one_button_w = panel.w - 11 - 17 - 36;
  const bool multi = input_.measure_height(ui_.gfx, input_style, one_button_w) + 20 > 42;
  if (multi != multi_) {
    multi_ = multi;
    multi_since_ = ui_.now_ms;
  }
  const float text_w = multi ? panel.w - 11 - 17 - 17 : one_button_w;
  const float input_h = std::clamp(input_.measure_height(ui_.gfx, input_style, text_w) + 20, 42.0f, 130.0f);
  const Rect compose{panel.x + 5, panel.bottom() - 4 - input_h, panel.w - 11, input_h};
  const Rect area{panel.x, panel.y, panel.w, panel.h - 10 - input_h};
  area_ = area;

  // --- message list: rows are in list units, which the zoom maps to the window ---
  const float list_w = area.w / zoom_ - 17 - 21;
  const float content_before = content_h_;
  const std::size_t shown_before = rows_.size();
  sync_rows(list_w);
  content_h_ = (next_y_ - 14 + 24) * zoom_;
  scroll_.view_h = area.h;
  scroll_.content_h = content_h_;
  const float max_scroll = scroll_.max_offset();
  // Older messages arrived above: what is on screen stays where it is.
  if (older_pending_ && !stick_ && rows_.size() > shown_before) {
    scroll_.offset += content_h_ - content_before;
    scroll_.target += content_h_ - content_before;
  }
  older_pending_ = false;
  if (stick_ && rows_.size() > shown_before && shown_before > 0) scroll_.target = max_scroll;  // a message came
  else if (stick_ && scroll_.target != max_scroll) scroll_.jump_to(max_scroll);  // the view changed size
  scroll_.clamp();
  scroll_.advance(ui_);

  p.rt()->PushAxisAlignedClip(D2D1::RectF(area.x, area.y, area.right(), area.bottom()),
                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  const bool fade = push_edge_fade(ui_, p, area, scroll_.fade_top(), scroll_.fade_bottom());
  ui_.add_hit_window(this, area, Area);
  ui_.push_hit_clip(area);
  const float list_top = area.y - scroll_.drawn();
  p.set_base(D2D1::Matrix3x2F::Scale(zoom_, zoom_) * D2D1::Matrix3x2F::Translation(area.x, list_top));
  constexpr float x = 17;
  HoverTip::Spec tip;
  bool show_tip = false;
  std::size_t in_view_from = rows_.size(), in_view_to = 0;
  for (std::size_t r = 0; r < rows_.size(); ++r) {
    Row& row = rows_[r];
    const float row_top = list_top + (row.head ? row.y_head : row.y_text) * zoom_;
    const float row_bottom = list_top + (row.y_text + row.h_text) * zoom_;
    if (row_bottom <= area.y) continue;
    if (row_top >= area.bottom()) break;
    in_view_from = std::min(in_view_from, r);
    in_view_to = r + 1;
    const std::size_t index = first_ + r;
    const ChatEntry& e = log_[index];
    // .chat-line entering: rises and fades in, the same way a member arrives.
    float arrived = 1;
    if (row.arrived_at != 0) {
      arrived = transition(ui_, row.arrived_at, arrive_ms);
      if (arrived >= 1) row.arrived_at = 0;
      else ui_.more_frames = true;
    }
    const float rise = arrive_rise * (1 - arrived);
    const Color ink = with_alpha(t.hl_text, arrived);
    if (row.head) {
      draw_svg(p, ui_.emoji_image(e.emoji), {x, row.y_head + rise, 25, 25}, arrived);
      const TextStyle time_style{.size = 14, .line_height = 20, .align = Align::Right};
      if (!row.time) {
        row.time = ui_.gfx.layout(widen(chat_clock_text(e.time_ms, use_24h_)), time_style, list_w, 20);
        DWRITE_TEXT_METRICS m{};
        if (row.time) row.time->GetMetrics(&m);
        row.time_w = m.widthIncludingTrailingWhitespace;
      }
      if (row.time) p.draw_layout(row.time.get(), x, row.y_head + 2.5f + rise, ink);
      if (!row.nick) {
        const TextStyle nick_style{.size = 15, .line_height = 20, .ellipsis = true};
        row.nick = ui_.gfx.layout(widen(e.nick.empty() ? std::string_view("게스트") : std::string_view(e.nick)), nick_style,
                                  std::max(20.0f, list_w - 25 - 7 - row.time_w - 7), 20);
      }
      if (row.nick) p.draw_layout(row.nick.get(), x + 25 + 7, row.y_head + 2.5f + rise, ink);
    }
    if (!row.text) row.text = ui_.gfx.layout(widen(e.text), body_style, list_w, 100000);
    if (!row.text) continue;
    IDWriteTextLayout* const layout = row.text.get();
    for (std::size_t l = 0; l < row.links.size() && l < links_per_entry; ++l) {
      const LinkSpan link = row.links[l];
      const int id = LinkBase + static_cast<int>(dropped_ + index) * links_per_entry + static_cast<int>(l);
      // A link that wraps has one box per line; the tip goes over all of them.
      DWRITE_HIT_TEST_METRICS boxes[8];
      UINT32 count = 0;
      if (FAILED(layout->HitTestTextRange(link.start, link.length, 0, 0, boxes, 8, &count))) continue;
      Rect bounds{};
      for (UINT32 b = 0; b < count; ++b) {
        const Rect box{area.x + (x + boxes[b].left) * zoom_, list_top + (row.y_text + boxes[b].top) * zoom_,
                       boxes[b].width * zoom_, boxes[b].height * zoom_};
        ui_.add_hit_window(this, box, id);
        if (b == 0) {
          bounds = box;
        } else {
          const float right = std::max(bounds.right(), box.right()), bottom = std::max(bounds.bottom(), box.bottom());
          bounds.x = std::min(bounds.x, box.x);
          bounds.y = std::min(bounds.y, box.y);
          bounds.w = right - bounds.x;
          bounds.h = bottom - bounds.y;
        }
      }
      layout->SetUnderline(ui_.hot(this, id), {link.start, link.length});
      if (ui_.hot(this, id)) {
        tip.text = "링크 연결";
        tip.anchor = bounds;
        tip.gap = 6;
        tip.slide = 0;
        show_tip = true;
      }
    }
    p.draw_layout(layout, x, row.y_text + rise, ink);
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
  p.set_base(D2D1::Matrix3x2F::Identity());
  pop_edge_fade(p, fade);
  p.rt()->PopAxisAlignedClip();

  // --- compose box ---
  p.fill_round(compose, 8, t.hl_mid);
  const Color ink = t.dark ? t.hl_text : t.on_accent;
  const Rect box{compose.x + 17, compose.y + 10, text_w, input_h - 20};
  const bool focused = ui_.focus == &input_;
  const Rect caret = input_.paint(p, box, input_style, ink, L"메시지 입력...", input_style, with_alpha(ink, 0.85f), ink,
                                  with_alpha(t.hl, 0.4f), focused, ui_.caret_on);
  if (focused) ui_.caret = caret;
  ui_.add_hit_window(this, compose, Input);

  float shake_dx = 0;
  if (shaking()) {
    shake_dx = keyframes(static_cast<float>(ui_.now_ms - std::min(ui_.now_ms, shake_at_)) / send_shake_ms, send_shake);
  }
  // .chat-send-anchor: bottom goes from 10 px to -34 px.
  const float out = multi_progress();
  const Rect anchor{panel.right() - 12 - 30 + shake_dx, panel.bottom() - 10 - 30 + 44 * out, 30, 30};
  paint_send(p, anchor, out);
  paint_calm(p, anchor);
  if (show_tip && tip.anchor.y - 6 - 32 < panel.y + 4) {
    tip.anchor.y = tip.anchor.bottom() + 6 + 32 + 6;  // no room above: below it
  }
  link_tip_.paint(ui_, p, show_tip ? &tip : nullptr);
}

void ChatPanel::paint_send(Painter& p, Rect send_rect, float out) {
  const Theme& t = ui_.theme;
  const bool hot = ui_.hot(this, Send);
  if (hot) p.push(scale_about(send_rect.x + 15, send_rect.y + 15, 1.08f));
  const float a = input_.text().empty() || limiter_.cooling() ? 0.55f : 1.0f;
  p.fill_ellipse(send_rect.x + 15, send_rect.y + 15, 15, 15, with_alpha(mix(t.hl, t.surface, out * 100), a));
  {
    p.push(D2D1::Matrix3x2F::Scale(18 / 24.f, 18 / 24.f) * D2D1::Matrix3x2F::Translation(send_rect.x + 6, send_rect.y + 6));
    p.stroke_geometry(ui_.gfx.path("M5 12h14M13 6l6 6-6 6"), with_alpha(mix(t.on_accent, t.hl_text, out * 100), a), 3);
    p.pop();
  }
  if (hot) p.pop();
  ui_.add_hit_window(this, send_rect, Send);
}

void ChatPanel::paint_calm(Painter& p, Rect anchor) {
  if (limiter_.calm() != calm_shown_) {
    calm_shown_ = limiter_.calm();
    calm_since_ = ui_.now_ms;
    if (calm_shown_) calm_text_ = limiter_.calm_text();
  }
  if (calm_shown_) calm_text_ = limiter_.calm_text();
  if (calm_since_ == 0) return;
  const float elapsed = ui_.animations ? static_cast<float>(ui_.now_ms - std::min(ui_.now_ms, calm_since_)) : 1e6f;
  float scale, alpha;
  if (calm_shown_) {
    scale = 0.05f + 0.95f * bezier_ease(0.3f, 1.7f, 0.5f, 1, elapsed / calm_in_ms);
    alpha = std::min(1.0f, elapsed / 100);
  } else {
    if (elapsed >= calm_out_ms) return;
    scale = 1 - 0.95f * bezier_ease(0.5f, 0, 0.9f, 0.6f, elapsed / calm_out_ms);
    alpha = 1 - std::clamp((elapsed - 20) / 120, 0.0f, 1.0f);
  }
  const Theme& t = ui_.theme;
  const TextStyle style{.size = 34, .weight = 800, .line_height = 34 * 1.35f};
  DWRITE_TEXT_METRICS m{};
  if (const auto layout = ui_.gfx.layout(widen(calm_text_), style, 2000)) layout->GetMetrics(&m);
  const float w = m.widthIncludingTrailingWhitespace + 2 * 28 + 1, h = 34 * 1.35f + 2 * 12;
  const float right = anchor.right(), bottom = anchor.bottom() - 54;
  const Rect r{right - w, bottom - h, w, h};
  // transform-origin: calc(100% - 15px) calc(100% + 39px), the send button's middle.
  const D2D1_MATRIX_3X2_F grow = scale_about(right - 15, bottom + 39, scale);
  p.push(grow);
  p.shadow_round(r, 28, 18, 6, rgb(0x2e648c, 0.28f * alpha));
  p.fill_round(r, 28, with_alpha(t.hl, alpha));
  p.text(calm_text_, {r.x + 28, r.y + 12, w - 56, h - 24}, style, with_alpha(t.on_accent, alpha));
  p.pop();
  if (const auto tail = ui_.gfx.parse_path(calm_tail)) {
    p.push(D2D1::Matrix3x2F::Translation(right - 10 - 36, bottom + 20 - 36) * grow);
    p.fill_geometry(tail.get(), with_alpha(t.hl, alpha));
    p.pop();
  }
}

void ChatPanel::click(int id, float, float, bool) {
  if (id >= LinkBase) {
    const auto number = static_cast<std::size_t>(id - LinkBase) / links_per_entry;
    const auto link = static_cast<std::size_t>(id - LinkBase) % links_per_entry;
    if (number < dropped_ || number - dropped_ >= log_.size()) return;
    // From the message itself: until the next paint rows_ may not match log_, and this
    // address goes to the shell.
    const std::wstring text = widen(log_[number - dropped_].text);
    const std::vector<LinkSpan> links = find_links(text);
    if (link >= links.size()) return;
    const std::string url = narrow(text.substr(links[link].start, links[link].length));
    if (on_open_link && !on_open_link(url) && ui_.toasts) ui_.toasts->toast("링크를 열지 못했어요.");
    return;
  }
  if (id != Input && id != Send) return;
  ui_.set_focus(this, &input_);
  if (id == Send) send();
}

bool ChatPanel::wheel(float x, float y, float delta) {
  if (!area_.contains(x, y)) return false;
  scroll_.wheel(ui_, delta);
  stick_ = scroll_.target >= scroll_.max_offset() - 4;
  // Close to the top the next page is laid out, without moving what is on screen.
  if (scroll_.target < 60 && first_ > 0) {
    first_ = first_ > chat_page ? first_ - chat_page : 0;
    dirty_ = true;
    older_pending_ = true;
  }
  return true;
}

void ChatPanel::enter(bool shift) {
  if (shift) input_.insert(std::wstring_view(L"\n", 1));
  else send();
}

bool ChatPanel::key(unsigned vk, bool ctrl, bool shift) {
  if (vk == VK_UP || vk == VK_DOWN) {
    const float w = area_.w - 11 - 17 - 36;
    input_.move_vertical(ui_.gfx, {.size = 17, .line_height = 22, .wrap = true}, w, vk == VK_UP ? -1 : 1, shift);
    return true;
  }
  return input_.key_down(vk, ctrl, shift);
}

}  // namespace crpp::ui
