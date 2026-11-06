#include <cmath>

#include "check.hpp"
#include "crpp/ui/controls.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
using test::UiHarness;

namespace {

void test_dialog_confirm_and_escape() {
  UiHarness h;
  CHECK(h.start());
  int confirmed = 0, cancelled = 0;
  auto dialog = [&] {
    return Dialog{"떠날까요?", "떠나기", "취소", false, [&] { ++confirmed; }, [&] { ++cancelled; }};
  };
  h.app.dialogs().show(dialog());
  h.render();
  CHECK(h.click(h.app.dialogs(), Dialogs::Confirm));
  CHECK(confirmed == 1);
  CHECK(!h.app.dialogs().is_open());

  h.app.dialogs().show(dialog());
  h.render();
  h.key(VK_ESCAPE);
  CHECK(cancelled == 1);
  CHECK(!h.app.dialogs().is_open());
}

void test_dialog_without_cancel_ignores_escape() {
  UiHarness h;
  CHECK(h.start());
  h.app.dialogs().show({"알아 두세요", "확인", "", false, nullptr, nullptr});
  h.render();
  CHECK(!h.drawn(h.app.dialogs(), Dialogs::Cancel));
  h.key(VK_ESCAPE);
  CHECK(h.app.dialogs().is_open());
}

void test_dialog_blocks_what_is_under_it() {
  UiHarness h;
  CHECK(h.start());
  int created = 0;
  h.app.home().on_create = [&] { ++created; };
  h.app.dialogs().show({"질문", "네", "아니요", false, nullptr, nullptr});
  h.render();
  h.app.mouse_down(180, 260, false);  // the create cloud
  CHECK(created == 0);
  CHECK(h.app.dialogs().is_open());
}

void test_settings_open_tab_and_close() {
  UiHarness h;
  CHECK(h.start());
  SettingsPanel& s = h.app.settings();
  CHECK(!s.is_open());
  CHECK(h.click(s, SettingsPanel::Droplet));
  CHECK(s.is_open());
  CHECK(h.click(s, SettingsPanel::TabBase + 2));
  CHECK(s.page() == SettingsPage::Notify);
  h.key(VK_ESCAPE);
  CHECK(!s.is_open());
  CHECK(s.page() == SettingsPage::Notify);  // stays where the user was
}

void test_settings_hide_room_controls() {
  UiHarness h;
  CHECK(h.start());
  int left = 0;
  h.app.room().on_leave = [&] { ++left; };
  h.app.show_session("ABC234");
  h.render();
  h.app.settings().open(SettingsPage::Sound);
  h.render();
  h.app.mouse_down(39, 692 - 39, false);  // where the leave button is otherwise
  CHECK(left == 0);
}

void test_toast_expires_without_frames() {
  UiHarness h;
  CHECK(h.start());
  Toasts& t = h.app.toasts();
  t.toast("서버 연결이 종료되었습니다.", 5000);
  h.render();
  const auto wake = h.app.wake_at();
  CHECK(wake != 0);
  CHECK(!h.app.animating());
  CHECK(h.app.tick(wake + 1000));  // gone once it has slid away
  CHECK(h.app.wake_at() == 0);
}

void test_toast_action_runs_once() {
  UiHarness h;
  CHECK(h.start());
  int ran = 0;
  h.app.toasts().toast("업데이트가 있어요", "받기", [&] { ++ran; });
  h.render();
  CHECK(h.click(h.app.toasts(), Toasts::Action));
  CHECK(ran == 1);
  CHECK(!h.drawn(h.app.toasts(), Toasts::Action));
}

void test_home_error_needs_no_button() {
  UiHarness h;
  CHECK(h.start());
  h.app.home().show_error("구름을 찾지 못했어요.");
  h.render();  // falls back to a toast
  CHECK(h.app.wake_at() != 0);
}

void test_hit_clip_cuts_hidden_content() {
  UiHarness h;
  CHECK(h.start());
  Ui& ui = h.app.ui();
  SetupScreen& owner = h.app.setup();
  ui.push_hit_clip({0, 0, 100, 100});
  ui.add_hit_window(&owner, {50, 50, 100, 100}, 7);
  ui.add_hit_window(&owner, {200, 200, 10, 10}, 8);
  ui.pop_hit_clip();
  const Ui::Hit* clipped = ui.find_hit(&owner, 7);
  CHECK(clipped && clipped->rect.w == 50 && clipped->rect.h == 50);
  CHECK(ui.find_hit(&owner, 8) == nullptr);
}

void test_slider_value_ends_and_middle() {
  const Rect r{10, 0, 121, 22};
  CHECK(slider_value(r, 0, 0, 100) == 0);
  CHECK(slider_value(r, 1000, 0, 100) == 100);
  CHECK(slider_value(r, 10 + 21.0f / 2 + 50, 0, 100) == 50);
  CHECK(slider_value(r, 1000, 55, 90) == 90);
}

void test_dropdown_selects_and_closes() {
  Dropdown d;
  d.options = {"구름", "먹구름", "파스텔"};
  d.id_base = 10;
  CHECK(d.owns(10) && d.owns(13) && !d.owns(14));
  CHECK(!d.click(10));
  CHECK(d.open);
  CHECK(d.click(12));
  CHECK(d.selected == 1);
  CHECK(!d.open);
  d.open = true;
  CHECK(!d.click(12));  // the same one changes nothing
  CHECK(!d.open);
}

void test_scroll_clamps_and_drags() {
  Scroll s{.offset = 0, .content_h = 1000, .view_h = 500};
  s.wheel(1);
  CHECK(s.offset == 0);
  s.wheel(-100);
  CHECK(s.offset == 500);
  const Rect track{0, 0, 8, 400};
  s.drag_thumb(track, 0);
  CHECK(s.offset == 0);
  s.drag_thumb(track, 400);
  CHECK(s.offset == 500);
  Scroll fits{.offset = 40, .content_h = 100, .view_h = 500};
  fits.clamp();
  CHECK(fits.offset == 0);
}

void test_ease_matches_css() {
  CHECK(ease(0) == 0);
  CHECK(std::fabs(ease(1) - 1) < 0.001f);
  CHECK(std::fabs(ease(0.5f) - 0.8024f) < 0.01f);  // cubic-bezier(.25,.1,.25,1) at x = .5
}

void test_prefs_reset_restores_defaults() {
  MemoryPrefs prefs;
  prefs.set(pref::volume, 5);
  prefs.set(pref::listen_self, true);
  prefs.set(pref::theme, "pastel");
  prefs.reset();
  CHECK(prefs.get(pref::volume) == 30);
  CHECK(!prefs.get(pref::listen_self));
  CHECK(prefs.get(pref::theme) == "cloudroom");
}

}  // namespace

int main() {
  test_dialog_confirm_and_escape();
  test_dialog_without_cancel_ignores_escape();
  test_dialog_blocks_what_is_under_it();
  test_settings_open_tab_and_close();
  test_settings_hide_room_controls();
  test_toast_expires_without_frames();
  test_toast_action_runs_once();
  test_home_error_needs_no_button();
  test_hit_clip_cuts_hidden_content();
  test_slider_value_ends_and_middle();
  test_dropdown_selects_and_closes();
  test_scroll_clamps_and_drags();
  test_ease_matches_css();
  test_prefs_reset_restores_defaults();
  return crpp_test_result();
}
