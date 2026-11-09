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
    return Dialog{"떠날까요?", "나가기", "취소", false, [&] { ++confirmed; }, [&] { ++cancelled; }};
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
  h.app.mouse_up(180, 260);
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

void test_clicks_in_a_row_count_only_close_together() {
  ClickStreak s;
  for (int i = 0; i < 4; ++i) CHECK(!s.click(1000 + i * 400, 5));
  CHECK(s.click(2600, 5));
  CHECK(!s.click(2700, 5));  // counting again from one
  ClickStreak slow;
  for (int i = 0; i < 4; ++i) CHECK(!slow.click(1000 + i * 400, 5));
  CHECK(!slow.click(1000 + 3 * 400 + ClickStreak::gap_ms + 1, 5));  // too late: this is the first again
}

void test_the_droplet_splashes_after_five_clicks() {
  UiHarness h;
  CHECK(h.start());
  SettingsPanel& s = h.app.settings();
  int splashed = 0;
  s.on_splash = [&] { ++splashed; };
  for (int i = 0; i < SettingsPanel::splash_clicks - 1; ++i) CHECK(h.click(s, SettingsPanel::Droplet));
  CHECK(s.splashes() == 0);
  CHECK(h.click(s, SettingsPanel::Droplet));
  CHECK(s.splashes() == 1);
  CHECK(splashed == 1);
  CHECK(s.is_open());  // it still opened and closed on every click
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
  h.app.mouse_up(39, 692 - 39);
  CHECK(left == 0);
}

void test_toast_expires_without_frames() {
  UiHarness h;
  CHECK(h.start());
  Toasts& t = h.app.toasts();
  t.toast("서버와의 연결이 끊어졌어요.", 5000);
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
  Ui instant;
  instant.animations = false;
  Scroll s{.offset = 0, .content_h = 1000, .view_h = 500};
  s.wheel(instant, 1);
  CHECK(s.offset == 0);
  s.wheel(instant, -100);
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

void test_scroll_heads_for_where_the_wheel_sent_it() {
  Ui ui;
  ui.now_ms = 1000;
  Scroll s{.offset = 0, .content_h = 1000, .view_h = 500};
  s.wheel(ui, -1);
  CHECK(s.target == 80 && s.offset == 0);
  CHECK(s.moving());
  float last = 0;
  for (int frame = 0; frame < 10; ++frame) {
    ui.now_ms += 16;
    ui.more_frames = false;
    s.advance(ui);
    CHECK(ui.more_frames);
    CHECK(s.offset > last || s.offset == s.target);
    last = s.offset;
  }
  CHECK(s.offset > 70 && s.offset <= 80);
  for (int frame = 0; frame < 30; ++frame) {
    ui.now_ms += 16;
    s.advance(ui);
  }
  CHECK(s.offset == 80);
  CHECK(!s.moving());
  ui.more_frames = false;
  s.advance(ui);
  CHECK(!ui.more_frames);  // a settled list asks for nothing
}

void test_scroll_stretches_past_the_end_and_comes_back() {
  Ui ui;
  ui.now_ms = 1000;
  Scroll s{.offset = 500, .content_h = 1000, .view_h = 500, .target = 500};
  for (int notch = 0; notch < 40; ++notch) s.wheel(ui, -1);
  CHECK(s.target == 500);
  CHECK(s.stretch > 20);
  CHECK(s.stretch <= EngineMotion{}.scroll_stretch);
  CHECK(s.drawn() > 500);
  CHECK(s.fade_bottom() == 0);
  const float pulled = s.stretch;
  ui.now_ms += 16;
  s.advance(ui);
  CHECK(s.stretch == pulled);  // the wheel may still be turning
  for (int frame = 0; frame < 40; ++frame) {
    ui.now_ms += 16;
    s.advance(ui);
  }
  CHECK(s.stretch == 0);
  CHECK(!s.moving());

  // Up past the top likewise, on the other side.
  s.jump_to(0);
  s.wheel(ui, 3);
  CHECK(s.stretch < 0);
  CHECK(s.fade_top() == 0);

  // What fits does not move at all.
  Scroll fits{.offset = 0, .content_h = 100, .view_h = 500};
  fits.wheel(ui, -3);
  CHECK(fits.stretch == 0);
  CHECK(!fits.moving());
}

void test_edge_fades_grow_with_the_distance() {
  Scroll s{.offset = 0, .content_h = 1000, .view_h = 500};
  CHECK(s.fade_top() == 0);
  CHECK(s.fade_bottom() == edge_fade_px);
  s.jump_to(10);
  CHECK(s.fade_top() == 10);
  s.jump_to(200);
  CHECK(s.fade_top() == edge_fade_px);
  s.jump_to(495);
  CHECK(s.fade_bottom() == 5);
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
  test_clicks_in_a_row_count_only_close_together();
  test_the_droplet_splashes_after_five_clicks();
  test_settings_hide_room_controls();
  test_toast_expires_without_frames();
  test_toast_action_runs_once();
  test_home_error_needs_no_button();
  test_hit_clip_cuts_hidden_content();
  test_slider_value_ends_and_middle();
  test_dropdown_selects_and_closes();
  test_scroll_clamps_and_drags();
  test_scroll_heads_for_where_the_wheel_sent_it();
  test_scroll_stretches_past_the_end_and_comes_back();
  test_edge_fades_grow_with_the_distance();
  test_ease_matches_css();
  test_prefs_reset_restores_defaults();
  return crpp_test_result();
}
