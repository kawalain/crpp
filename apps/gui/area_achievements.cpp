#include <string>

#include "areas.hpp"
#include "crpp/ui/achievements.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui::area::achievements {

namespace {

// A key of this client's own, beside the settings: the totals and the day each Achievement was reached.
constexpr std::string_view record_key = "achievementRecord";

store::Json record(const Achievements& a) {
  store::Json json = store::Json::object();
  const Achievements::Totals& t = a.totals();
  json.set("presses", store::Json(t.presses));
  json.set("clicks", store::Json(t.clicks));
  json.set("minutes", store::Json(t.minutes));
  json.set("messages", store::Json(t.messages));
  store::Json reached = store::Json::object();
  for (const AchievementInfo& info : achievement_table) {
    if (const auto day = a.reached_on(info.id)) reached.set(info.key, store::Json(day));
  }
  json.set("reached", std::move(reached));
  return json;
}

void load(Shell& s) {
  const store::Json* json = s.store->find(record_key);
  if (!json || !json->is_object()) return;
  const auto count = [&](std::string_view key) -> std::uint64_t {
    const store::Json* v = json->find(key);
    const auto n = v ? v->as_i64() : std::nullopt;
    return n && *n > 0 ? static_cast<std::uint64_t>(*n) : 0;
  };
  Achievements::Totals totals{count("presses"), count("clicks"), count("minutes"), count("messages")};
  std::array<Achievements::Day, achievement_count> reached{};
  if (const store::Json* days = json->find("reached")) {
    for (const AchievementInfo& info : achievement_table) {
      const store::Json* v = days->find(info.key);
      const auto day = v ? v->as_i64() : std::nullopt;
      // yyyymmdd; anything else was not written here.
      if (day && *day >= 19700101 && *day <= 99991231) reached[static_cast<std::size_t>(info.id)] = static_cast<Achievements::Day>(*day);
    }
  }
  s.app.achievements().load(totals, reached);
}

void write(Shell& s) { s.store->set(record_key, record(s.app.achievements())); }

}  // namespace

void wire(Shell& s) {
  Achievements& a = s.app.achievements();
  load(s);
  a.set_enabled(s.prefs->get(pref::achievements_on));
  Shell* sp = &s;
  s.app.on_achievement = [sp](AchievementId) {
    os::achievement_sound(*sp);
    write(*sp);
    (void)sp->store->save();
  };
  s.app.on_left_room = [sp] { flush(*sp); };
  s.app.settings().achievements().on_clear = [sp] {
    write(*sp);
    (void)sp->store->save();
  };
}

void flush(Shell& s) {
  if (s.store && s.app.achievements().take_changed()) write(s);
}

void pref_changed(Shell& s, std::string_view key) {
  if (key == pref::achievements_on.name) s.app.achievements().set_enabled(s.prefs->get(pref::achievements_on));
}

bool scene(Shell& s, std::string_view name) {
  // settings-achievements: part of the way, a few reached; -end scrolled to the bottom.
  if (name != "settings-achievements" && name != "settings-achievements-end") return false;
  std::array<Achievements::Day, achievement_count> reached{};
  const auto mark = [&](AchievementId id, Achievements::Day day) { reached[static_cast<std::size_t>(id)] = day; };
  mark(AchievementId::Keys10k, 20231020);
  mark(AchievementId::Clicks10k, 20231101);
  mark(AchievementId::Hours10, 20231105);
  mark(AchievementId::Room4, 20231020);
  mark(AchievementId::Splash, 20231021);
  mark(AchievementId::WordCake, 20231110);
  s.app.achievements().load({37512, 21043, 2190, 64}, reached);
  s.app.settings().open(SettingsPage::Achievements);
  if (name == "settings-achievements-end") s.app.settings().scroll_to(100000);
  return true;
}

}  // namespace crpp::gui::area::achievements
