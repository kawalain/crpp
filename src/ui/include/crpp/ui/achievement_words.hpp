#pragma once
// What an Achievement is called and asks for, in the current Locale.
#include <cstdint>
#include <string>

#include "crpp/ui/achievements.hpp"

namespace crpp::ui {

[[nodiscard]] const char* achievement_name(AchievementId id);
// "키를 10,000번 누르기"
[[nodiscard]] std::string achievement_task(AchievementId id);
[[nodiscard]] const char* achievement_group_name(AchievementGroup group);
// "37,512": digits in groups of three.
[[nodiscard]] std::string group_digits(std::uint64_t n);
// "2026.11.09" from yyyymmdd.
[[nodiscard]] std::string format_day(Achievements::Day day);

}  // namespace crpp::ui
