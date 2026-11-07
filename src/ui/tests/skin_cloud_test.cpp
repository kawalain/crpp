// Cloud drawn from its pack in skins/: in a window that scales the stage, the same places to click as
// the C++ layout, and motion that the pack states and that ends. Pixels are compared with tests/shots.
#include <cmath>
#include <string>
#include <vector>

#include "check.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
using test::UiHarness;

namespace {

constexpr const char* cloud_pack = CRPP_TEST_SKINS "/cloud";

struct Place {
  const Surface* owner;
  int id;
};

std::vector<Rect> rects_of(UiHarness& h, const std::vector<Place>& places) {
  h.render();
  std::vector<Rect> found;
  for (const Place& place : places) {
    const Ui::Hit* hit = h.app.ui().find_hit(place.owner, place.id);
    CHECK(hit != nullptr);
    found.push_back(hit ? hit->rect : Rect{});
  }
  return found;
}

void same_places(UiHarness& h, const std::vector<Place>& places) {
  const std::vector<Rect> code = rects_of(h, places);
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  CHECK(h.app.ui().palette >= 0);
  const std::vector<Rect> pack = rects_of(h, places);
  for (std::size_t i = 0; i < places.size(); ++i) {
    CHECK(std::abs(pack[i].x - code[i].x) < 0.01f && std::abs(pack[i].y - code[i].y) < 0.01f);
    CHECK(std::abs(pack[i].w - code[i].w) < 0.01f && std::abs(pack[i].h - code[i].h) < 0.01f);
  }
}

void test_the_layers_stand_where_the_code_puts_them() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_home();
  h.app.toasts().toast("A message", "Do it", [] {});
  h.app.update_popup().show_available();
  h.app.notice().set(Notice{"A longer notice that takes more than one line of this window to say what it wants to say", "Act", "Later"});
  h.app.dialogs().show({"Is this a question that wraps on to a second line of its own?", "Yes", "No", false, nullptr, nullptr});
  h.app.offline().show();
  const Surface* dialogs = &h.app.dialogs();
  const Surface* offline = &h.app.offline();
  const Surface* notice = &h.app.notice();
  const Surface* update = &h.app.update_popup();
  same_places(h, {{dialogs, Dialogs::Backdrop}, {dialogs, Dialogs::Card}, {dialogs, Dialogs::Confirm}, {dialogs, Dialogs::Cancel},
                  {offline, OfflinePopup::Backdrop}, {offline, OfflinePopup::Card},
                  {notice, NoticeCard::Card}, {notice, NoticeCard::Action}, {notice, NoticeCard::Dismiss},
                  {update, UpdatePopup::Card}, {update, UpdatePopup::Button},
                  {&h.app.toasts(), Toasts::Action}});
}

void test_the_prompt_stands_where_the_code_puts_it_and_clicks_land() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  PromptScreen& prompt = h.app.prompt();
  h.app.show_prompt({.heading = "Hello", .label = "Name", .placeholder = "name", .hint = "A hint", .value = "abc", .prefix = "@", .extra = "One", .extra2 = "Two", .link = "A link", .password = true, .cancelable = true, .valid = [](std::string_view v) { return !v.empty(); }});
  h.render();
  const Rect caret_of_code = h.app.ui().caret;
  same_places(h, {{&prompt, PromptScreen::Field}, {&prompt, PromptScreen::Submit}, {&prompt, PromptScreen::Eye}, {&prompt, PromptScreen::Back},
                  {&prompt, PromptScreen::Extra}, {&prompt, PromptScreen::Extra2}, {&prompt, PromptScreen::Link}});
  const Rect caret = h.app.ui().caret;
  CHECK(std::abs(caret.x - caret_of_code.x) < 0.01f && std::abs(caret.y - caret_of_code.y) < 0.01f);
  CHECK(!prompt.password_shown());
  CHECK(h.click(prompt, PromptScreen::Eye));
  CHECK(prompt.password_shown());
  std::string submitted;
  prompt.on_submit = [&](std::string value) { submitted = std::move(value); };
  CHECK(h.click(prompt, PromptScreen::Submit));
  CHECK(submitted == "abc");
}

void test_a_list_stands_where_the_code_puts_it() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_home();
  h.app.settings().open(SettingsPage::Display);
  DisplayTab& display = h.app.settings().display();
  display.click(DisplayTab::ThemeList, 0, 0, false);
  same_places(h, {{&display, DisplayTab::ThemeList + 1}, {&display, DisplayTab::ThemeList + 2}});
}

// The clock is held, so that a frame's time is what the test says.
class Moving {
 public:
  explicit Moving(UiHarness& h) : h_(h) {
    h_.app.set_animations(true);
    at(1000);
  }
  ~Moving() { hold_clock_at(0); }
  Moving(const Moving&) = delete;
  Moving& operator=(const Moving&) = delete;
  // The time goes on by this much, and a frame is drawn.
  bool after(unsigned long long ms) {
    at(now_ + ms);
    (void)h_.app.tick(now_);
    h_.render();
    return h_.app.animating();
  }

 private:
  void at(unsigned long long ms) {
    now_ = ms;
    hold_clock_at(ms);
  }
  UiHarness& h_;
  unsigned long long now_ = 0;
};

void test_a_dialog_rises_and_then_asks_for_no_more_frames() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_home();
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  h.render();
  Moving moving(h);
  h.app.dialogs().show({"Sure?", "Yes", "No", false, nullptr, nullptr});
  CHECK(moving.after(0));
  CHECK(moving.after(80));
  CHECK(!moving.after(80));
  CHECK(!moving.after(1000));
  h.app.dialogs().close();
}

void test_a_toast_comes_goes_and_is_gone() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_home();
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  h.render();
  Moving moving(h);
  bool acted = false;
  h.app.toasts().toast("A message", "Do it", [&] { acted = true; }, 1000);
  CHECK(moving.after(0));
  CHECK(h.drawn(h.app.toasts(), Toasts::Action));
  CHECK(moving.after(90));
  CHECK(!moving.after(90));
  CHECK(!moving.after(400));
  CHECK(h.app.toasts().wake_at() != 0);
  CHECK(moving.after(420));  // past its time, it slides away
  CHECK(h.drawn(h.app.toasts(), Toasts::Action));
  CHECK(moving.after(90));
  CHECK(!moving.after(90));
  CHECK(!h.drawn(h.app.toasts(), Toasts::Action));
  CHECK(h.app.toasts().wake_at() == 0);
  CHECK(!acted);
}

void test_a_tip_fades_out_before_it_lets_go_of_its_text() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_session("ABC234");
  h.app.room().set_room({{0, "Me", "", false, "c0"}, {1, "You", "", false, "c1"}}, 0, 0, false, true);
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  h.render();
  RoomScreen& room = h.app.room();
  Moving moving(h);
  h.app.ui().hover_owner = &room;
  h.app.ui().hover_id = RoomScreen::Leave;
  CHECK(moving.after(0));
  CHECK(room.hover_tip().visible());
  CHECK(!moving.after(150));
  const std::string words = room.hover_tip().text();
  CHECK(!words.empty());
  h.app.ui().hover_owner = nullptr;
  h.app.ui().hover_id = 0;
  CHECK(moving.after(0));
  CHECK(!room.hover_tip().visible());
  CHECK(room.hover_tip().text() == words);
  (void)moving.after(150);
  CHECK(room.hover_tip().text().empty());
  CHECK(!moving.after(1000));
}

void test_the_offline_popup_rises_once() {
  UiHarness h;
  CHECK(h.start(1200, 900));
  h.app.show_home();
  CHECK(h.app.load_skin_dir(cloud_pack).has_value());
  h.render();
  Moving moving(h);
  h.app.offline().show();
  CHECK(moving.after(0));
  CHECK(!moving.after(160));
  CHECK(!moving.after(0));  // it is on screen and settled, and walks on without a transition
  h.app.offline().hide();
  (void)moving.after(0);
  h.app.offline().show();
  CHECK(moving.after(0));  // shown anew, it rises anew
}

}  // namespace

int main() {
  test_the_layers_stand_where_the_code_puts_them();
  test_the_prompt_stands_where_the_code_puts_it_and_clicks_land();
  test_a_list_stands_where_the_code_puts_it();
  test_a_dialog_rises_and_then_asks_for_no_more_frames();
  test_a_toast_comes_goes_and_is_gone();
  test_a_tip_fades_out_before_it_lets_go_of_its_text();
  test_the_offline_popup_rises_once();
  return crpp_test_result();
}
