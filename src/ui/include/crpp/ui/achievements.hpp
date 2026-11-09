#pragma once
// Achievements: goals reached by using the app, kept on this machine only. What is kept is four totals (key
// presses, clicks, minutes in Rooms, messages sent) and the day each Achievement was reached: no key, no text
// and no time of day. Pure and portable ISO C++; counting allocates nothing.
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>

namespace crpp::ui {

enum class AchievementId : std::uint8_t {
  Keys10k, Keys100k, Keys1m, Clicks10k, Clicks100k, Clicks1m,
  Hours10, Hours100, Hours1000, Room4, Room8, Room16, Messages100, Messages1000,
  Splash, SpecialDay, CalmFirm, WordZzz, WordCake, WordStar, Konami,
  Storm, Named, NightOwl,
};
inline constexpr std::size_t achievement_count = 24;

enum class AchievementGroup : std::uint8_t { Input, Together, Discovery, Hidden };
// What an Achievement counts towards its goal; None for one reached by doing something once.
enum class Tally : std::uint8_t { None, Presses, Clicks, Minutes, Messages, RoomSize };

struct AchievementInfo {
  AchievementId id;
  std::string_view key;  // its name in the saved record
  AchievementGroup group;
  Tally tally;
  std::uint64_t goal;  // for a tally; minutes for Minutes
  bool secret;         // shown as "???" until reached
};

// In the order the list shows them.
inline constexpr std::array<AchievementInfo, achievement_count> achievement_table{{
    {AchievementId::Keys10k, "keys_10k", AchievementGroup::Input, Tally::Presses, 10'000, false},
    {AchievementId::Keys100k, "keys_100k", AchievementGroup::Input, Tally::Presses, 100'000, false},
    {AchievementId::Keys1m, "keys_1m", AchievementGroup::Input, Tally::Presses, 1'000'000, false},
    {AchievementId::Clicks10k, "clicks_10k", AchievementGroup::Input, Tally::Clicks, 10'000, false},
    {AchievementId::Clicks100k, "clicks_100k", AchievementGroup::Input, Tally::Clicks, 100'000, false},
    {AchievementId::Clicks1m, "clicks_1m", AchievementGroup::Input, Tally::Clicks, 1'000'000, false},
    {AchievementId::Hours10, "hours_10", AchievementGroup::Together, Tally::Minutes, 10 * 60, false},
    {AchievementId::Hours100, "hours_100", AchievementGroup::Together, Tally::Minutes, 100 * 60, false},
    {AchievementId::Hours1000, "hours_1000", AchievementGroup::Together, Tally::Minutes, 1000 * 60, false},
    {AchievementId::Room4, "room_4", AchievementGroup::Together, Tally::RoomSize, 4, false},
    {AchievementId::Room8, "room_8", AchievementGroup::Together, Tally::RoomSize, 8, false},
    {AchievementId::Room16, "room_16", AchievementGroup::Together, Tally::RoomSize, 16, false},
    {AchievementId::Messages100, "messages_100", AchievementGroup::Together, Tally::Messages, 100, false},
    {AchievementId::Messages1000, "messages_1000", AchievementGroup::Together, Tally::Messages, 1000, false},
    {AchievementId::Splash, "splash", AchievementGroup::Discovery, Tally::None, 1, false},
    {AchievementId::SpecialDay, "special_day", AchievementGroup::Discovery, Tally::None, 1, false},
    {AchievementId::CalmFirm, "calm_firm", AchievementGroup::Discovery, Tally::None, 1, false},
    {AchievementId::WordZzz, "word_zzz", AchievementGroup::Discovery, Tally::None, 1, true},
    {AchievementId::WordCake, "word_cake", AchievementGroup::Discovery, Tally::None, 1, true},
    {AchievementId::WordStar, "word_star", AchievementGroup::Discovery, Tally::None, 1, true},
    {AchievementId::Konami, "konami", AchievementGroup::Discovery, Tally::None, 1, true},
    {AchievementId::Storm, "storm", AchievementGroup::Hidden, Tally::None, 1, true},
    {AchievementId::Named, "named", AchievementGroup::Hidden, Tally::None, 1, true},
    {AchievementId::NightOwl, "night_owl", AchievementGroup::Hidden, Tally::None, 1, true},
}};

[[nodiscard]] constexpr const AchievementInfo& info(AchievementId id) { return achievement_table[static_cast<std::size_t>(id)]; }

class Achievements {
 public:
  // A day, as yyyymmdd by the local calendar.
  using Day = std::int32_t;

  struct Totals {
    std::uint64_t presses = 0, clicks = 0, minutes = 0, messages = 0;
  };

  // Called for each Achievement as it is reached.
  std::function<void(AchievementId)> on_reached;

  // Off, nothing counts and nothing is reached; what was reached stays.
  void set_enabled(bool on) { enabled_ = on; }
  [[nodiscard]] bool enabled() const { return enabled_; }

  // Adds to a total and reaches what it now passes.
  void add(Tally tally, std::uint64_t n, Day today);
  // So many members are online in the Room.
  void room_size(std::size_t online, Day today);
  // Something done once.
  void reach(AchievementId id, Day today);

  [[nodiscard]] const Totals& totals() const { return totals_; }
  [[nodiscard]] std::uint64_t total(Tally tally) const;
  // The day it was reached, or 0.
  [[nodiscard]] Day reached_on(AchievementId id) const { return reached_[static_cast<std::size_t>(id)]; }
  [[nodiscard]] std::size_t reached_count() const;

  // Time in a Room: from entering it, whole minutes go to the total as they pass.
  void enter_room(std::uint64_t now_ms) { room_since_ = now_ms; }
  void in_room(std::uint64_t now_ms, Day today);
  void leave_room(std::uint64_t now_ms, Day today) {
    in_room(now_ms, today);
    room_since_ = 0;
  }

  // What a saved record held; nothing is reached by loading.
  void load(const Totals& totals, const std::array<Day, achievement_count>& reached) {
    totals_ = totals;
    reached_ = reached;
  }
  [[nodiscard]] const std::array<Day, achievement_count>& reached() const { return reached_; }
  // Forgets every total and every Achievement.
  void clear() {
    totals_ = {};
    reached_ = {};
  }
  // Whether something changed since the last call, to be written.
  [[nodiscard]] bool take_changed() { return std::exchange(changed_, false); }

 private:
  void mark(AchievementId id, Day today);
  void check(Tally tally, std::uint64_t value, Day today);

  Totals totals_;
  std::array<Day, achievement_count> reached_{};
  std::uint64_t room_since_ = 0;
  bool enabled_ = true;
  bool changed_ = false;
};

}  // namespace crpp::ui
