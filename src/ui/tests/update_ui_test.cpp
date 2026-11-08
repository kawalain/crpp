// The update popup and the changelog viewer, driven without a window.
#include <optional>

#include "check.hpp"
#include "crpp/update/notes.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
using test::UiHarness;

namespace {

ChangelogEntry entry(std::string version, std::string body) {
  return {"", "2026년 11월 04일", std::move(version), crpp::update::parse_notes(body)};
}

std::vector<ChangelogEntry> three_short() {
  std::vector<ChangelogEntry> entries;
  entries.push_back(entry("0.3.0", "## 변경 사항\n- 셋"));
  entries.push_back(entry("0.2.0", "- 둘"));
  entries.push_back(entry("0.1.0", "- 하나"));
  return entries;
}

std::string long_body() {
  std::string body = "## 긴 목록\n";
  for (int i = 0; i < 60; ++i) body += "- 항목 " + std::to_string(i) + "\n";
  return body;
}

void wheel(UiHarness& h, float notches, int times = 1) {
  for (int i = 0; i < times; ++i) {
    h.app.wheel(475, 340, notches);
    h.render();
  }
}

void the_popup_is_hidden_until_told() {
  UiHarness h;
  CHECK(h.start());
  UpdatePopup& popup = h.app.update_popup();
  CHECK(!h.drawn(popup, UpdatePopup::Card));
  popup.show_available();
  h.render();
  CHECK(h.drawn(popup, UpdatePopup::Button));
  popup.hide();
  h.render();
  CHECK(!h.drawn(popup, UpdatePopup::Card));
}

void the_button_starts_the_update_and_goes_away_while_busy() {
  UiHarness h;
  CHECK(h.start());
  UpdatePopup& popup = h.app.update_popup();
  int pressed = 0;
  popup.on_update = [&] {
    ++pressed;
    popup.show_progress(0);
  };
  popup.show_available();
  h.render();
  CHECK(h.click(popup, UpdatePopup::Button));
  CHECK(pressed == 1);
  CHECK(popup.state() == UpdatePopup::State::Busy);
  CHECK(h.drawn(popup, UpdatePopup::Card));
  CHECK(!h.drawn(popup, UpdatePopup::Button));

  popup.show_progress(250);
  CHECK(popup.percent() == 100);
  h.render();

  popup.show_failed();
  h.render();
  CHECK(h.click(popup, UpdatePopup::Button));  // 다시 시도
  CHECK(pressed == 2);
}

void the_popup_sits_at_the_top_left_on_every_view() {
  UiHarness h;
  CHECK(h.start());
  UpdatePopup& popup = h.app.update_popup();
  popup.show_available();
  const auto check_placed = [&] {
    h.render();
    const Ui::Hit* card = h.app.ui().find_hit(&popup, UpdatePopup::Card);
    CHECK(card != nullptr);
    if (card) CHECK(card->rect.x == 14 && card->rect.y == 14);
    CHECK(h.drawn(popup, UpdatePopup::Button));
  };
  h.app.show_home();
  check_placed();
  h.app.show_session("ABC123");
  check_placed();
  h.app.show_setup({.files_done = 3, .files = 10});
  check_placed();
  h.app.show_home();
  h.app.settings().open(SettingsPage::Sound);
  check_placed();
  // Clickable above the settings overlay too.
  int pressed = 0;
  popup.on_update = [&] { ++pressed; };
  CHECK(h.click(popup, UpdatePopup::Button));
  CHECK(pressed == 1);
}

void a_check_waits_with_a_single_wake_up() {
  UiHarness h;
  CHECK(h.start());
  UpdatePopup& popup = h.app.update_popup();
  int due = 0;
  popup.on_check_due = [&] { ++due; };
  CHECK(h.app.wake_at() == 0);
  CHECK(!h.app.animating());
  popup.schedule_check(1000);
  CHECK(h.app.wake_at() == 1000);
  CHECK(!h.app.animating());  // waiting needs no frames
  h.app.tick(999);
  CHECK(due == 0);
  h.app.tick(1000);
  CHECK(due == 1);
  CHECK(h.app.wake_at() == 0);  // gone until the next one is scheduled
  h.app.tick(2000);
  CHECK(due == 1);
}

void the_version_button_says_what_it_does_and_opens() {
  UiHarness h;
  CHECK(h.start());
  h.app.settings().set_version("0.3.0");
  h.app.settings().open(SettingsPage::Sound);
  h.render();
  int opened = 0;
  h.app.settings().on_version = [&] { ++opened; };
  CHECK(h.click(h.app.settings(), SettingsPanel::Version));
  CHECK(opened == 1);
}

class FakeClipboard final : public Clipboard {
 public:
  bool set_text(std::wstring_view text) override {
    held = text;
    return true;
  }
  std::optional<std::wstring> text() override { return held; }
  bool has_text() override { return held.has_value(); }
  std::optional<std::wstring> held;
};

// The notes are text to select and copy, line by line, in either Skin.
void the_notes_can_be_selected_and_copied(const char* pack) {
  UiHarness h;
  CHECK(h.start(951, 692, pack));
  FakeClipboard clipboard;
  h.app.ui().clipboard = &clipboard;
  ChangelogView& view = h.app.changelog();
  view.open({entry("0.3.0", "## Changes\n- first item\n- second item")}, 0);
  if (h.app.open_panes().size() > 0) h.on_pane(Pane::Changelog);  // a window of its own under Retro
  h.render();
  const Ui::Hit* last = h.app.ui().find_hit(&view, ChangelogView::Text);
  CHECK(last != nullptr);
  if (!last) return;
  const Rect line = last->rect;
  const float y = line.y + 3;
  hold_clock_at(5000000);
  h.app.mouse_move(line.x + 1, y);
  h.app.mouse_down(line.x + 1, y, false);
  h.app.mouse_move(line.x + 600, y);
  h.app.mouse_up(line.x + 600, y);
  CHECK(view.selected_text() == L"second item");
  CHECK(h.app.key_down('C', true, false) && clipboard.held == L"second item");
  CHECK(h.app.key_down('A', true, false));
  CHECK(view.selected_text() == L"Changes\nfirst item\nsecond item");
  hold_clock_at(0);
}

void opens_closes_and_keeps_nothing() {
  UiHarness h;
  CHECK(h.start());
  ChangelogView& view = h.app.changelog();
  view.open(three_short(), 1);
  h.render();
  CHECK(view.is_open() && view.index() == 1);
  CHECK(h.click(view, ChangelogView::Close));
  CHECK(!view.is_open());
  CHECK(view.count() == 0);

  view.open(three_short(), 0);
  h.render();
  h.key(VK_ESCAPE);
  CHECK(!view.is_open());

  view.open(three_short(), 0);
  h.render();
  h.app.mouse_move(5, 5);  // the backdrop, outside the card
  h.app.mouse_down(5, 5, false);
  h.app.mouse_up(5, 5);
  CHECK(!view.is_open());
}

void a_clamped_index_and_no_data_are_fine() {
  UiHarness h;
  CHECK(h.start());
  ChangelogView& view = h.app.changelog();
  view.open(three_short(), 99);
  CHECK(view.index() == 2);
  h.render();
  view.open({}, 0);
  h.render();
  CHECK(view.is_open());
  wheel(h, -1, 20);  // nothing to step to
  CHECK(view.index() == 0);
  CHECK(h.click(view, ChangelogView::Close));
}

void the_wheel_steps_through_the_hint_to_the_next_version() {
  UiHarness h;
  CHECK(h.start());
  ChangelogView& view = h.app.changelog();
  view.open(three_short(), 0);
  h.render();
  // Upward from the newest goes nowhere.
  wheel(h, 1, 20);
  CHECK(view.index() == 0 && view.hint() == 0);

  wheel(h, -1, ChangelogOverscroll::hint_at - 1);
  CHECK(view.hint() == 0);
  wheel(h, -1);
  CHECK(view.hint() == 1);
  wheel(h, -1, ChangelogOverscroll::switch_after - 1);
  CHECK(view.index() == 0 && view.hint() == 1);
  wheel(h, -1);
  CHECK(view.index() == 1 && view.hint() == 0);

  // And back.
  wheel(h, 1, ChangelogOverscroll::hint_at);
  CHECK(view.hint() == -1);
  wheel(h, 1, ChangelogOverscroll::switch_after);
  CHECK(view.index() == 0);
}

void a_long_version_scrolls_before_it_steps() {
  UiHarness h;
  CHECK(h.start());
  ChangelogView& view = h.app.changelog();
  std::vector<ChangelogEntry> entries;
  entries.push_back(entry("0.2.0", long_body()));
  entries.push_back(entry("0.1.0", "- 하나"));
  view.open(std::move(entries), 0);
  h.render();
  // 60 items are far more than ten notches: this is scrolling, not stepping.
  wheel(h, -1, 10);
  CHECK(view.index() == 0 && view.hint() == 0);
  wheel(h, -1, 60);  // to the end, and well past it
  CHECK(view.index() == 1);
}

void turning_back_starts_the_count_over() {
  ChangelogOverscroll counter;
  CHECK(counter.step(1, 1000, true, true, 1).hint == 0);
  CHECK(counter.step(1, 1100, true, true, 1).hint == 0);
  CHECK(counter.step(-1, 1200, true, true, 1).hint == 0);
  for (int i = 0; i < 3; ++i) CHECK(counter.step(1, 1300, true, true, 1).hint == 0);
  CHECK(counter.step(1, 1300, true, true, 1).hint == 0);
  CHECK(counter.step(1, 1300, true, true, 1).hint == 1);
}

void counts_like_upstream() {
  ChangelogOverscroll counter;
  // A big notch counts for as many.
  CHECK(counter.step(1, 1000, true, true, 5).hint == 1);
  // The hint is still appearing: this does not count.
  CHECK(counter.step(1, 1100, true, true, 5).go == 0);
  CHECK(counter.step(1, 1200, true, true, 5).go == 1);
  // A trackpad's stream is one input per gap.
  ChangelogOverscroll pad;
  unsigned long long now = 1000;
  for (int i = 0; i < 50; ++i) CHECK(pad.step(1, now += 10, true, true, 0.1f).hint == 0);
  for (int i = 0; i < 3; ++i) CHECK(pad.step(1, now += 200, true, true, 0.1f).hint == 0);
  CHECK(pad.step(1, now += 200, true, true, 0.1f).hint == 1);
  // Not at the edge, or nowhere to go: nothing counts.
  ChangelogOverscroll idle;
  CHECK(idle.step(1, 1000, false, true, 5).hint == 0);
  CHECK(idle.step(1, 1000, true, false, 5).hint == 0);
  CHECK(!idle.counting());
}

void an_idle_wheel_forgets_the_count() {
  UiHarness h;
  CHECK(h.start());
  ChangelogView& view = h.app.changelog();
  view.open(three_short(), 0);
  h.render();
  CHECK(h.app.wake_at() == 0);
  wheel(h, -1, ChangelogOverscroll::hint_at);
  CHECK(view.hint() == 1);
  const auto at = h.app.wake_at();
  CHECK(at != 0);
  h.app.tick(at);
  CHECK(view.hint() == 0);
  CHECK(h.app.wake_at() == 0);
}

void new_data_keeps_the_version_in_view() {
  UiHarness h;
  CHECK(h.start());
  ChangelogView& view = h.app.changelog();
  view.open(three_short(), 1);
  auto more = three_short();
  more.insert(more.begin(), entry("0.4.0", "- 넷"));
  view.set_entries(std::move(more));
  h.render();
  CHECK(view.index() == 2 && view.count() == 4);
}

void nothing_beneath_it_takes_the_wheel_or_a_click() {
  UiHarness h;
  CHECK(h.start());
  ChangelogView& view = h.app.changelog();
  UpdatePopup& popup = h.app.update_popup();
  int pressed = 0;
  popup.on_update = [&] { ++pressed; };
  popup.show_available();
  view.open(three_short(), 0);
  h.render();
  CHECK(h.app.wheel(10, 10, -1));
  const Ui::Hit* button = h.app.ui().find_hit(&popup, UpdatePopup::Button);
  CHECK(button != nullptr);
  if (button) {
    const Ui::Hit* top = h.app.ui().hit_at(button->rect.x + 2, button->rect.y + 2);
    CHECK(top != nullptr && top->owner == &view);
  }
}

}  // namespace

int main() {
  the_notes_can_be_selected_and_copied(CRPP_TEST_SKINS "/cloud");
  the_notes_can_be_selected_and_copied(CRPP_TEST_SKINS "/retro");
  the_popup_is_hidden_until_told();
  the_button_starts_the_update_and_goes_away_while_busy();
  the_popup_sits_at_the_top_left_on_every_view();
  a_check_waits_with_a_single_wake_up();
  the_version_button_says_what_it_does_and_opens();
  opens_closes_and_keeps_nothing();
  a_clamped_index_and_no_data_are_fine();
  the_wheel_steps_through_the_hint_to_the_next_version();
  a_long_version_scrolls_before_it_steps();
  turning_back_starts_the_count_over();
  counts_like_upstream();
  an_idle_wheel_forgets_the_count();
  new_data_keeps_the_version_in_view();
  nothing_beneath_it_takes_the_wheel_or_a_click();
  return crpp_test_result();
}
