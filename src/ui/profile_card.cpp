#include "crpp/ui/profile_card.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "crpp/ui/controls.hpp"
#include "crpp/ui/toasts.hpp"

namespace crpp::ui {

namespace {

constexpr float card_w = 380, pad_top = 24, pad_x = 25, pad_bottom = 20;
constexpr float icon_size = 72, icon_emoji = 46, head_gap = 18, head_bottom = 20;
constexpr float stat_line = 17 * 1.9f;

std::string format_date(std::int64_t seconds) {
  const auto t = static_cast<std::time_t>(seconds);
  std::tm tm{};
  localtime_s(&tm, &t);
  char date[32];
  std::snprintf(date, sizeof date, "%d.%02d.%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
  return date;
}

}  // namespace

std::string format_stay(std::int64_t seconds) {
  std::int64_t minutes = std::max<std::int64_t>(0, seconds / 60);
  const std::int64_t years = minutes / 525600;
  minutes %= 525600;
  const std::int64_t months = minutes / 43200;
  minutes %= 43200;
  const std::int64_t hours = minutes / 60;
  minutes %= 60;
  std::string out;
  const auto part = [&](std::int64_t value, const char* unit) {
    if (value == 0) return;
    if (!out.empty()) out += ' ';
    out += std::to_string(value) + unit;
  };
  part(years, "년");
  part(months, "개월");
  part(hours, "시간");
  part(minutes, "분");
  return out.empty() ? "1분 미만" : out;
}

std::string format_count(std::uint64_t count) {
  std::string digits = std::to_string(count);
  for (std::size_t at = digits.size(); at > 3;) {
    at -= 3;
    digits.insert(at, 1, ',');
  }
  return digits + "회";
}

void ProfileCard::request(std::uint32_t slot) {
  pending_ = slot;
  give_up_at_ = clock_ms() + answer_timeout_ms;
  if (on_request) on_request(slot);
}

void ProfileCard::answer(ProfileView profile, bool mine) {
  const bool awaited = pending_ && *pending_ == profile.slot;
  const bool refresh = open_ && profile_.slot == profile.slot;
  if (!awaited && !refresh) return;
  if (awaited) pending_.reset();
  profile_ = std::move(profile);
  mine_ = mine;
  if (refresh) return;  // only the numbers move
  open_ = true;
  opened_at_ = clock_ms();
  was_animating_ = true;
  refresh_at_ = opened_at_ + refresh_ms;
  ui_.set_focus(nullptr, nullptr);
}

void ProfileCard::close() {
  open_ = false;
  pending_.reset();
  was_animating_ = false;
}

void ProfileCard::paint(Painter& p) {
  if (!open_) return;
  const Theme& t = ui_.theme;
  const int rows = profile_.created ? 3 : 2;
  const float w = std::min(card_w, ui_.width - 40);
  const float h = pad_top + icon_size + head_bottom + static_cast<float>(rows) * stat_line + pad_bottom;
  const float shown = transition(ui_, opened_at_, modal_pop_ms);
  const Rect card = paint_modal(ui_, this, p, w, h, shown, Backdrop, Card);

  const Rect icon{card.x + pad_x, card.y + pad_top, icon_size, icon_size};
  const bool hot = mine_ && ui_.hot(this, Icon);
  const float grow = hot ? 1.05f : 1.0f;
  const float cx = icon.x + icon_size / 2, cy = icon.y + icon_size / 2;
  p.fill_ellipse(cx, cy, icon_size / 2 * grow, icon_size / 2 * grow, with_alpha(hot ? t.hl_pale2 : t.hl_pale, shown));
  const float emoji = icon_emoji * grow;
  draw_svg(p, ui_.emoji_image(profile_.emoji), {cx - emoji / 2, cy - emoji / 2, emoji, emoji}, shown);
  if (mine_) ui_.add_hit_window(this, icon, Icon);

  const std::string nick = profile_.nick.empty() ? "게스트" : profile_.nick;
  const float names_x = icon.right() + head_gap;
  const float names_w = card.right() - pad_x - names_x;
  const Color ink = with_alpha(t.hl_text, shown);
  const bool two_lines = profile_.handle && nick != "@" + *profile_.handle;
  if (two_lines) {
    const float top = icon.y + (icon_size - (30 + 2 + 20)) / 2;
    p.text(nick, {names_x, top, names_w, 30}, {.size = 24, .weight = 700, .line_height = 30, .ellipsis = true}, ink);
    p.text("@" + *profile_.handle, {names_x, top + 32, names_w, 20}, {.size = 16, .line_height = 20, .ellipsis = true},
           with_alpha(t.hl_text_soft, shown));
  } else {
    p.text(nick, {names_x, icon.y + (icon_size - 34) / 2, names_w, 34},
           {.size = 28, .weight = 700, .line_height = 34, .ellipsis = true}, ink);
  }

  float y = icon.bottom() + head_bottom;
  const TextStyle stat{.size = 17, .line_height = stat_line, .ellipsis = true};
  const auto row = [&](const std::string& text) {
    p.text(text, {card.x + pad_x, y, w - 2 * pad_x, stat_line}, stat, ink);
    y += stat_line;
  };
  if (profile_.created) row("가입 시기: " + format_date(*profile_.created));
  row("이 구름에서 머무른 시간: " + format_stay(profile_.stayed));
  row("이 구름에서 입력한 횟수: " + format_count(profile_.events));

  HoverTip::Spec spec;
  spec.text = "아이콘 변경";
  spec.anchor = icon;
  tip_.paint(ui_, p, hot ? &spec : nullptr);
}

void ProfileCard::click(int id, float, float, bool) {
  if (id == Backdrop) close();
  else if (id == Icon && mine_ && on_change_icon) on_change_icon();
}

bool ProfileCard::escape() {
  if (!open_) return false;
  close();
  return true;
}

bool ProfileCard::animating() const { return open_ && ((ui_.animations && was_animating_) || tip_.animating(ui_)); }

bool ProfileCard::tick(unsigned long long now_ms) {
  bool changed = open_ && tip_.animating(ui_);
  if (was_animating_) {
    changed = true;
    if (static_cast<float>(now_ms - std::min(now_ms, opened_at_)) >= modal_pop_ms) was_animating_ = false;
  }
  if (pending_ && now_ms >= give_up_at_) {
    pending_.reset();
    if (ui_.toasts) ui_.toasts->toast("프로필을 불러오지 못했어요.");
    changed = true;
  }
  if (open_ && now_ms >= refresh_at_) {
    refresh_at_ = now_ms + refresh_ms;
    if (on_request) on_request(profile_.slot);
  }
  return changed;
}

unsigned long long ProfileCard::wake_at() const {
  unsigned long long at = pending_ ? give_up_at_ : 0;
  if (open_ && (at == 0 || refresh_at_ < at)) at = refresh_at_;
  return at;
}

}  // namespace crpp::ui
