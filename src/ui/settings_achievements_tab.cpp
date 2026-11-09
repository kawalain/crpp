#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "crpp/locale/locale.hpp"
#include "crpp/skin/draw.hpp"
#include "crpp/ui/achievement_words.hpp"
#include "crpp/ui/dialogs.hpp"
#include "crpp/ui/settings_tabs.hpp"
#include "crpp/ui/skin_canvas.hpp"

namespace crpp::ui {

namespace {

using locale::tr;

enum TextId : int { Summary, RowTitle, RowText, RowNote };
enum NumberId : int { On, Rows, RowHeading, RowReached, RowSecret, RowHasProgress, RowFraction, ReachedN, TotalN };
constexpr skin::Name texts[] = {{"summary", Summary}, {"row_title", RowTitle}, {"row_text", RowText}, {"row_note", RowNote}};
constexpr skin::Name numbers[] = {{"on", On}, {"rows", Rows}, {"row_heading", RowHeading}, {"row_reached", RowReached}, {"row_secret", RowSecret},
                                  {"row_has_progress", RowHasProgress}, {"row_fraction", RowFraction}, {"reached", ReachedN}, {"total", TotalN}};
constexpr skin::Name hits[] = {{"toggle", AchievementsTab::Toggle}, {"clear", AchievementsTab::Clear}};
constexpr skin::Name reports[] = {{"height", 0}};

constexpr AchievementGroup groups[] = {AchievementGroup::Input, AchievementGroup::Together, AchievementGroup::Discovery, AchievementGroup::Hidden};

// One line of the list: a group's heading, or an Achievement under it.
struct Row {
  bool heading = false;
  bool reached = false, secret = false, has_progress = false;
  float fraction = 0;
  std::string title, text, note;
};

std::vector<Row> rows_of(const Achievements* a) {
  std::vector<Row> rows;
  rows.reserve(std::size(groups) + achievement_count);
  for (const AchievementGroup group : groups) {
    rows.push_back({.heading = true, .title = achievement_group_name(group)});
    for (const AchievementInfo& info : achievement_table) {
      if (info.group != group) continue;
      const Achievements::Day day = a ? a->reached_on(info.id) : 0;
      Row row;
      row.reached = day != 0;
      row.secret = info.secret && !row.reached;
      if (row.secret) {
        row.title = tr().ach_secret;
        row.text = tr().ach_secret_hint;
      } else {
        row.title = achievement_name(info.id);
        row.text = achievement_task(info.id);
      }
      if (row.reached) {
        row.note = locale::fill(tr().ach_reached_on, {format_day(day)});
      } else if (info.tally != Tally::None && info.tally != Tally::RoomSize) {
        // Hours show as hours, the rest as they are counted.
        const bool hours = info.tally == Tally::Minutes;
        const std::uint64_t done = a ? a->total(info.tally) : 0;
        const std::uint64_t shown = hours ? done / 60 : done, goal = hours ? info.goal / 60 : info.goal;
        row.has_progress = true;
        row.fraction = static_cast<float>(static_cast<double>(std::min(done, info.goal)) / static_cast<double>(info.goal));
        row.note = locale::fill(tr().ach_progress, {group_digits(shown), group_digits(goal)});
      }
      rows.push_back(std::move(row));
    }
  }
  return rows;
}

}  // namespace

skin::ScreenSchema AchievementsTab::schema() {
  return {.screen = "settings_achievements", .texts = texts, .numbers = numbers, .hits = hits, .reports = reports};
}

void AchievementsTab::paint(Painter& p, Rect area) {
  if (const skin::Screen* screen = ui_.pack_screen("settings_achievements")) paint_pack(p, *screen, area);
}

void AchievementsTab::click(int id, float, float, bool) {
  if (id == Toggle) {
    ui_.prefs->set(pref::achievements_on, !ui_.prefs->get(pref::achievements_on));
  } else if (id == Clear && ui_.dialogs) {
    ui_.dialogs->show({tr().ach_clear_question, tr().ach_clear_confirm, tr().cancel, false, [this] {
                         if (ui_.achievements) ui_.achievements->clear(clock_ms());
                         if (on_clear) on_clear();
                       },
                       nullptr, true});
  }
}

void AchievementsTab::paint_pack(Painter& p, const skin::Screen& screen, Rect area) {
  class Shown final : public skin::Source {
   public:
    explicit Shown(AchievementsTab& tab) : s_(tab), rows_(rows_of(tab.ui_.achievements)) {
      const std::size_t reached = tab.ui_.achievements ? tab.ui_.achievements->reached_count() : 0;
      summary_ = locale::fill(tr().ach_summary, {std::to_string(reached), std::to_string(achievement_count)});
      reached_ = static_cast<float>(reached);
    }

    std::string_view text(int id, int index) override {
      if (id == Summary) return summary_;
      const Row* row = at(index);
      if (!row) return {};
      switch (id) {
        case RowTitle: return row->title;
        case RowText: return row->text;
        case RowNote: return row->note;
        default: return {};
      }
    }

    float number(int id, int index) override {
      const auto yes = [](bool b) { return b ? 1.0f : 0.0f; };
      switch (id) {
        case On: return yes(s_.ui_.prefs->get(pref::achievements_on));
        case Rows: return static_cast<float>(rows_.size());
        case ReachedN: return reached_;
        case TotalN: return static_cast<float>(achievement_count);
        default: break;
      }
      const Row* row = at(index);
      if (!row) return 0;
      switch (id) {
        case RowHeading: return yes(row->heading);
        case RowReached: return yes(row->reached);
        case RowSecret: return yes(row->secret);
        case RowHasProgress: return yes(row->has_progress);
        case RowFraction: return row->fraction;
        default: return 0;
      }
    }

    void report(int, float value) override { s_.flow_height_ = value; }

   private:
    [[nodiscard]] const Row* at(int index) const { return index >= 0 && static_cast<std::size_t>(index) < rows_.size() ? &rows_[static_cast<std::size_t>(index)] : nullptr; }

    AchievementsTab& s_;
    std::vector<Row> rows_;
    std::string summary_;
    float reached_ = 0;
  } shown(*this);

  SkinCanvas canvas(ui_, p, this);
  skin::draw(*ui_.pack, screen, ui_.palette, {area.x, area.y, area.w, area.h}, canvas, shown);
}

}  // namespace crpp::ui
