#include "crpp/ui/achievement_words.hpp"

#include <cstdio>

#include "crpp/locale/locale.hpp"

namespace crpp::ui {

namespace {
using locale::tr;
}  // namespace

const char* achievement_name(AchievementId id) {
  const locale::Strings& t = tr();
  switch (id) {
    case AchievementId::Keys10k: return t.ach_keys_10k;
    case AchievementId::Keys100k: return t.ach_keys_100k;
    case AchievementId::Keys1m: return t.ach_keys_1m;
    case AchievementId::Clicks10k: return t.ach_clicks_10k;
    case AchievementId::Clicks100k: return t.ach_clicks_100k;
    case AchievementId::Clicks1m: return t.ach_clicks_1m;
    case AchievementId::Hours10: return t.ach_hours_10;
    case AchievementId::Hours100: return t.ach_hours_100;
    case AchievementId::Hours1000: return t.ach_hours_1000;
    case AchievementId::Room4: return t.ach_room_4;
    case AchievementId::Room8: return t.ach_room_8;
    case AchievementId::Room16: return t.ach_room_16;
    case AchievementId::Messages100: return t.ach_messages_100;
    case AchievementId::Messages1000: return t.ach_messages_1000;
    case AchievementId::Splash: return t.ach_splash;
    case AchievementId::SpecialDay: return t.ach_special_day;
    case AchievementId::CalmFirm: return t.ach_calm_firm;
    case AchievementId::WordZzz: return t.ach_word_zzz;
    case AchievementId::WordCake: return t.ach_word_cake;
    case AchievementId::WordStar: return t.ach_word_star;
    case AchievementId::Konami: return t.ach_konami;
    case AchievementId::Storm: return t.ach_storm;
    case AchievementId::Named: return t.ach_named;
    case AchievementId::NightOwl: return t.ach_night_owl;
  }
  return "";
}

std::string achievement_task(AchievementId id) {
  const locale::Strings& t = tr();
  const AchievementInfo& a = info(id);
  switch (a.tally) {
    case Tally::Presses: return locale::fill(t.ach_do_presses, {group_digits(a.goal)});
    case Tally::Clicks: return locale::fill(t.ach_do_clicks, {group_digits(a.goal)});
    case Tally::Minutes: return locale::fill(t.ach_do_hours, {group_digits(a.goal / 60)});
    case Tally::Messages: return locale::fill(t.ach_do_messages, {group_digits(a.goal)});
    case Tally::RoomSize: return locale::fill(t.ach_do_room_size, {group_digits(a.goal)});
    case Tally::None: break;
  }
  switch (id) {
    case AchievementId::Splash: return t.ach_do_splash;
    case AchievementId::SpecialDay: return t.ach_do_special_day;
    case AchievementId::CalmFirm: return t.ach_do_calm_firm;
    case AchievementId::WordZzz: return locale::fill(t.ach_do_word, {"ZZZ"});
    case AchievementId::WordCake: return locale::fill(t.ach_do_word, {"CAKE"});
    case AchievementId::WordStar: return locale::fill(t.ach_do_word, {"STAR"});
    case AchievementId::Konami: return t.ach_do_konami;
    case AchievementId::Storm: return t.ach_do_storm;
    case AchievementId::Named: return t.ach_do_named;
    case AchievementId::NightOwl: return t.ach_do_night_owl;
    default: return {};
  }
}

const char* achievement_group_name(AchievementGroup group) {
  switch (group) {
    case AchievementGroup::Input: return tr().ach_group_input;
    case AchievementGroup::Together: return tr().ach_group_together;
    case AchievementGroup::Discovery: return tr().ach_group_discovery;
    case AchievementGroup::Hidden: return tr().ach_group_hidden;
  }
  return "";
}

std::string group_digits(std::uint64_t n) {
  std::string digits = std::to_string(n);
  for (std::size_t at = digits.size(); at > 3;) {
    at -= 3;
    digits.insert(at, 1, ',');
  }
  return digits;
}

std::string format_day(Achievements::Day day) {
  char text[16];
  std::snprintf(text, sizeof text, "%d.%02d.%02d", day / 10000, day / 100 % 100, day % 100);
  return text;
}

}  // namespace crpp::ui
