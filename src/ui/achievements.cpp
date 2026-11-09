#include "crpp/ui/achievements.hpp"

#include <algorithm>

namespace crpp::ui {

std::uint64_t Achievements::total(Tally tally) const {
  switch (tally) {
    case Tally::Presses: return totals_.presses;
    case Tally::Clicks: return totals_.clicks;
    case Tally::Minutes: return totals_.minutes;
    case Tally::Messages: return totals_.messages;
    default: return 0;
  }
}

std::size_t Achievements::reached_count() const {
  return static_cast<std::size_t>(std::ranges::count_if(reached_, [](Day d) { return d != 0; }));
}

void Achievements::mark(AchievementId id, Day today) {
  Day& day = reached_[static_cast<std::size_t>(id)];
  if (day != 0) return;
  day = today;
  changed_ = true;
  if (on_reached) on_reached(id);
}

void Achievements::check(Tally tally, std::uint64_t value, Day today) {
  for (const auto& a : achievement_table) {
    if (a.tally == tally && value >= a.goal) mark(a.id, today);
  }
}

void Achievements::add(Tally tally, std::uint64_t n, Day today) {
  if (!enabled_ || n == 0) return;
  switch (tally) {
    case Tally::Presses: totals_.presses += n; break;
    case Tally::Clicks: totals_.clicks += n; break;
    case Tally::Minutes: totals_.minutes += n; break;
    case Tally::Messages: totals_.messages += n; break;
    default: return;
  }
  changed_ = true;
  check(tally, total(tally), today);
}

void Achievements::room_size(std::size_t online, Day today) {
  if (!enabled_) return;
  check(Tally::RoomSize, online, today);
}

void Achievements::reach(AchievementId id, Day today) {
  if (enabled_) mark(id, today);
}

void Achievements::in_room(std::uint64_t now_ms, Day today) {
  if (room_since_ == 0 || now_ms < room_since_) return;
  const std::uint64_t minutes = (now_ms - room_since_) / 60000;
  if (minutes == 0) return;
  room_since_ += minutes * 60000;
  add(Tally::Minutes, minutes, today);
}

}  // namespace crpp::ui
