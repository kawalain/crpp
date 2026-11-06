#include "crpp/ui/chat_panel.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace crpp::ui {

namespace {

std::string chat_clock(long long ms) {
  const std::time_t t = static_cast<std::time_t>(ms / 1000);
  std::tm tm{};
  localtime_s(&tm, &t);
  const std::time_t now = std::time(nullptr);
  std::tm today{};
  localtime_s(&today, &now);
  const auto day_index = [](const std::tm& d) {
    std::tm copy = d;
    copy.tm_hour = copy.tm_min = copy.tm_sec = 0;
    return static_cast<long long>(std::mktime(&copy)) / 86400;
  };
  char clock[32];
  std::snprintf(clock, sizeof clock, "%s %d:%02d", tm.tm_hour < 12 ? "오전" : "오후",
                tm.tm_hour % 12 == 0 ? 12 : tm.tm_hour % 12, tm.tm_min);
  const long long days = day_index(today) - day_index(tm);
  if (days <= 0) return clock;
  if (days == 1) return std::string("어제 ") + clock;
  if (days == 2) return std::string("그저께 ") + clock;
  char date[16];
  std::snprintf(date, sizeof date, "%02d.%02d ", tm.tm_mon + 1, tm.tm_mday);
  return std::string(date) + clock;
}

bool chat_grouped(const ChatEntry& e, const ChatEntry* prev) {
  if (!prev || prev->slot != e.slot || prev->cid != e.cid) return false;
  const long long gap = e.time_ms - prev->time_ms;
  return gap >= 0 && gap <= 5 * 60 * 1000;
}

}  // namespace

ChatPanel::ChatPanel(Ui& ui) : Surface(ui) {
  input_.max_length = 200;
  input_.multiline = true;
}

void ChatPanel::add(ChatEntry entry) {
  log_.push_back(std::move(entry));
  if (log_.size() > 1000) log_.erase(log_.begin());
}

void ChatPanel::clear() {
  log_.clear();
  scroll_ = 0;
  stick_ = true;
  input_.clear();
}

void ChatPanel::send() {
  std::wstring text = input_.text();
  const auto blank = [](wchar_t c) { return c == L' ' || c == 0x0A; };
  while (!text.empty() && blank(text.back())) text.pop_back();
  std::size_t lead = 0;
  while (lead < text.size() && blank(text[lead])) ++lead;
  text.erase(0, lead);
  if (text.empty()) return;
  if (on_send) on_send(narrow(text));
  input_.clear();
}

void ChatPanel::paint(Painter& p, Rect panel) {
  const Theme& t = ui_.theme;
  const TextStyle input_style{.size = 17, .line_height = 22, .wrap = true};
  const float text_w = panel.w - 11 - 17 - 36;
  const float input_h = std::clamp(input_.measure_height(ui_.gfx, input_style, text_w) + 20, 42.0f, 130.0f);
  const Rect compose{panel.x + 5, panel.bottom() - 4 - input_h, panel.w - 11, input_h};
  const Rect area{panel.x, panel.y, panel.w, panel.h - 10 - input_h};
  area_ = area;

  // --- message list: lay everything out once, then draw the visible part ---
  struct Row {
    ComPtr<IDWriteTextLayout> text;
    const ChatEntry* entry;
    bool head;
    float y_head, y_text, h_text;
  };
  std::vector<Row> rows;
  const float list_w = area.w - 17 - 21;
  float y = 24;
  const std::size_t first = log_.size() > 100 ? log_.size() - 100 : 0;
  const TextStyle body_style{.size = 17, .line_height = 19, .wrap = true};
  for (std::size_t i = first; i < log_.size(); ++i) {
    const ChatEntry& e = log_[i];
    const bool cont = chat_grouped(e, i > first ? &log_[i - 1] : nullptr);
    Row row{ui_.gfx.layout(widen(e.text), body_style, list_w, 100000), &e, !cont, 0, 0, 0};
    if (cont) {
      y -= 10;
    } else {
      row.y_head = y;
      y += 25 + 2;
    }
    DWRITE_TEXT_METRICS m{};
    if (row.text) row.text->GetMetrics(&m);
    row.y_text = y;
    row.h_text = m.height;
    y += m.height + 14;
    rows.push_back(std::move(row));
  }
  content_h_ = y - 14 + 24;
  area_h_ = area.h;
  const float max_scroll = std::max(0.0f, content_h_ - area.h);
  if (stick_) scroll_ = max_scroll;
  scroll_ = std::clamp(scroll_, 0.0f, max_scroll);

  p.rt()->PushAxisAlignedClip(D2D1::RectF(area.x, area.y, area.right(), area.bottom()),
                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  for (const Row& r : rows) {
    const float x = area.x + 17;
    const float top = area.y - scroll_;
    if (r.head) {
      draw_svg(p, ui_.emoji_image(r.entry->emoji), {x, top + r.y_head, 25, 25});
      const std::string time = chat_clock(r.entry->time_ms);
      const D2D1_SIZE_F time_size = p.text(time, {x, top + r.y_head + 2.5f, list_w, 20},
                                           {.size = 14, .line_height = 20, .align = Align::Right}, t.hl_text);
      p.text(r.entry->nick.empty() ? "게스트" : r.entry->nick,
             {x + 25 + 7, top + r.y_head + 2.5f, std::max(20.0f, list_w - 25 - 7 - time_size.width - 7), 20},
             {.size = 15, .line_height = 20, .ellipsis = true}, t.hl_text);
    }
    if (r.text) p.draw_layout(r.text.get(), x, top + r.y_text, t.hl_text);
  }
  p.rt()->PopAxisAlignedClip();
  ui_.add_hit_window(this, area, Area);

  // --- compose box ---
  p.fill_round(compose, 8, t.hl_mid);
  const Color ink = t.dark ? t.hl_text : t.on_accent;
  const Rect box{compose.x + 17, compose.y + 10, text_w, input_h - 20};
  const bool focused = ui_.focus == &input_;
  const Rect caret = input_.paint(p, box, input_style, ink, L"메시지 입력...", input_style, with_alpha(ink, 0.85f), ink,
                                  with_alpha(t.hl, 0.4f), focused, ui_.caret_on);
  if (focused) ui_.caret = caret;
  ui_.add_hit_window(this, compose, Input);

  const Rect send_rect{panel.right() - 12 - 30, panel.bottom() - 10 - 30, 30, 30};
  const bool hot = ui_.hot(this, Send);
  if (hot) p.push(scale_about(send_rect.x + 15, send_rect.y + 15, 1.08f));
  const float a = input_.text().empty() ? 0.55f : 1.0f;
  p.fill_ellipse(send_rect.x + 15, send_rect.y + 15, 15, 15, with_alpha(t.surface, a));
  {
    const auto arrow = ui_.gfx.parse_path("M5 12h14M13 6l6 6-6 6");
    p.push(D2D1::Matrix3x2F::Scale(18 / 24.f, 18 / 24.f) * D2D1::Matrix3x2F::Translation(send_rect.x + 6, send_rect.y + 6));
    p.stroke_geometry(arrow.get(), with_alpha(t.hl_text, a), 3);
    p.pop();
  }
  if (hot) p.pop();
  ui_.add_hit_window(this, send_rect, Send);
}

void ChatPanel::click(int id, float, float, bool) {
  if (id != Input && id != Send) return;
  ui_.set_focus(this, &input_);
  if (id == Send) send();
}

bool ChatPanel::wheel(float x, float y, float delta) {
  if (!area_.contains(x, y)) return false;
  const float max_scroll = std::max(0.0f, content_h_ - area_h_);
  scroll_ = std::clamp(scroll_ - delta * 80, 0.0f, max_scroll);
  stick_ = scroll_ >= max_scroll - 4;
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
