#include "check.hpp"
#include "crpp/ui/settings_tabs.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
using test::UiHarness;

namespace {

struct Fixture {
  UiHarness h;
  std::vector<SoundChoice> speakers{{"id-a", "스피커 A"}, {"id-b", "스피커 B"}};
  bool started = h.start();
  SoundTab& tab = h.app.settings().sound();
  Prefs& prefs = *h.app.ui().prefs;

  Fixture() {
    CHECK(started);
    tab.set_speakers(speakers);
    tab.set_packs(SoundDevice::Keyboard, {{"plastic", "플라스틱"}, {"thocky", "쏘키"}, {"walnut", "호두"}});
    tab.set_packs(SoundDevice::Mouse, {{"plastic", "플라스틱"}, {"rubber", "고무"}, {"walnut", "호두"}});
    tab.set_packs(SoundDevice::Pen, {{"paper", "종이"}});
    h.app.settings().open(SettingsPage::Sound);
    h.render();
  }
  // Scrolled to the far end, where the effects are, for hits that are clipped otherwise.
  void show_end() {
    h.app.settings().scroll_to(10000);
    h.render();
  }
  void drag_to(int id, float fraction) {
    const Ui::Hit* hit = h.app.ui().find_hit(&tab, id);
    CHECK(hit != nullptr);
    if (!hit) return;
    const float y = hit->rect.y + hit->rect.h / 2;
    const float left = hit->rect.x, width = hit->rect.w;
    h.app.mouse_move(left + 1, y);
    h.app.mouse_down(left + 1, y, false);
    h.app.mouse_move(left + width * fraction, y);
    h.app.mouse_up(left + width * fraction, y);
    h.render();
  }
};

void the_controls_are_there_and_pen_is_hidden() {
  Fixture f;
  CHECK(f.h.drawn(f.tab, SoundTab::SpeakerVolume));
  CHECK(f.h.drawn(f.tab, SoundTab::KeyboardVolume));
  CHECK(f.h.drawn(f.tab, SoundTab::MouseVolume));
  CHECK(f.h.drawn(f.tab, SoundTab::SpeakerList));
  CHECK(f.h.drawn(f.tab, SoundTab::KeyboardList));
  CHECK(f.h.drawn(f.tab, SoundTab::MouseList));
  CHECK(show_pen == f.h.drawn(f.tab, SoundTab::PenVolume));
  CHECK(show_pen == f.h.drawn(f.tab, SoundTab::PenList));
  // Upstream's height, the balance control's bottom and the panel's padding, a line more,
  // and the rows of the reverb and the mute shortcut.
  CHECK(f.tab.content_height() == 973);
}

void sliders_change_their_settings_and_only_when_different() {
  Fixture f;
  f.drag_to(SoundTab::SpeakerVolume, 0.5f);
  CHECK(f.prefs.get(pref::volume) > 40 && f.prefs.get(pref::volume) < 60);
  f.drag_to(SoundTab::KeyboardVolume, 0);
  CHECK(f.prefs.get(pref::keyboard_volume) == 0);
  f.drag_to(SoundTab::MouseVolume, 1);
  CHECK(f.prefs.get(pref::mouse_volume) == 100);
}

void a_slider_shows_its_tip_on_hover_and_while_dragged() {
  Fixture f;
  CHECK(!f.tab.tip_shown());
  const Ui::Hit* hit = f.h.app.ui().find_hit(&f.tab, SoundTab::SpeakerVolume);
  CHECK(hit != nullptr);
  const float x = hit->rect.x + 20, y = hit->rect.y + hit->rect.h / 2;
  f.h.app.mouse_move(x, y);
  f.h.render();
  CHECK(f.tab.tip_shown());
  f.h.app.mouse_down(x, y, false);
  f.h.app.mouse_move(x + 400, y + 300);  // far off, still held
  f.h.render();
  CHECK(f.tab.tip_shown());
  CHECK(f.prefs.get(pref::volume) == 100);
  f.h.app.mouse_up(x + 400, y + 300);
  f.h.app.mouse_move(5, 5);
  f.h.render();
  CHECK(!f.tab.tip_shown());
}

void every_toggle_flips_its_setting() {
  Fixture f;
  const auto flips = [&](SoundTab::Id id, pref::Bool key) {
    const bool before = f.prefs.get(key);
    CHECK(f.h.click(f.tab, id));
    CHECK(f.prefs.get(key) != before);
    CHECK(f.h.click(f.tab, id));
    CHECK(f.prefs.get(key) == before);
  };
  flips(SoundTab::ListenSelf, pref::listen_self);
  flips(SoundTab::OthersUseMine, pref::others_use_my_sounds);
  f.show_end();
  flips(SoundTab::MuteNotifications, pref::mute_notifications_on_mute);
  flips(SoundTab::MuteOnIdle, pref::mute_on_idle);
  flips(SoundTab::Spatial, pref::spatial_audio);
  flips(SoundTab::BalanceEnabled, pref::channel_balance_enabled);
}

void the_balance_runs_from_55_to_90_and_can_be_off() {
  Fixture f;
  f.show_end();
  f.drag_to(SoundTab::Balance, 0);
  CHECK(f.prefs.get(pref::channel_balance) == 55);
  f.drag_to(SoundTab::Balance, 1);
  CHECK(f.prefs.get(pref::channel_balance) == 90);
  CHECK(f.h.click(f.tab, SoundTab::BalanceEnabled));
  CHECK(!f.prefs.get(pref::channel_balance_enabled));
  CHECK(!f.h.drawn(f.tab, SoundTab::Balance));
  CHECK(f.h.click(f.tab, SoundTab::BalanceEnabled));
  CHECK(f.h.drawn(f.tab, SoundTab::Balance));
}

void the_balance_is_said_in_words() {
  CHECK(balance_word(55) == "약함");
  CHECK(balance_word(60) == "약함");
  CHECK(balance_word(65) == "약함");
  CHECK(balance_word(66) == "보통");
  CHECK(balance_word(78) == "보통");
  CHECK(balance_word(79) == "강함");
  CHECK(balance_word(90) == "강함");
}

void the_balance_is_locked_without_the_surround() {
  Fixture f;
  f.show_end();
  f.drag_to(SoundTab::Balance, 1);
  CHECK(f.h.click(f.tab, SoundTab::Spatial));
  CHECK(!f.prefs.get(pref::spatial_audio));
  CHECK(!f.h.drawn(f.tab, SoundTab::Balance));
  CHECK(!f.h.drawn(f.tab, SoundTab::BalanceEnabled));
  CHECK(f.prefs.get(pref::channel_balance_enabled));  // kept for when it comes back
  CHECK(f.prefs.get(pref::channel_balance) == 90);
  CHECK(f.h.click(f.tab, SoundTab::Spatial));
  CHECK(f.h.drawn(f.tab, SoundTab::Balance));
  CHECK(f.h.drawn(f.tab, SoundTab::BalanceEnabled));
}

void the_reverb_is_picked_from_its_list_and_stays_without_the_surround() {
  Fixture f;
  f.show_end();
  CHECK(f.prefs.get(pref::reverb) == "office");
  CHECK(f.h.click(f.tab, SoundTab::ReverbList));
  CHECK(f.h.click(f.tab, SoundTab::ReverbList + 1));
  CHECK(f.prefs.get(pref::reverb) == "off");
  CHECK(f.h.click(f.tab, SoundTab::Spatial));
  CHECK(f.h.drawn(f.tab, SoundTab::ReverbList));
  CHECK(f.h.click(f.tab, SoundTab::ReverbList));
  CHECK(f.h.click(f.tab, SoundTab::ReverbList + 3));
  CHECK(f.prefs.get(pref::reverb) == "large");
  CHECK(!f.prefs.get(pref::spatial_audio));
  // Low in the view, so its list opens upward and covers what is above it.
  CHECK(f.h.click(f.tab, SoundTab::ReverbList));
  f.h.key(VK_ESCAPE);
  CHECK(!f.h.drawn(f.tab, SoundTab::ReverbList + 1));
  CHECK(f.h.app.settings().is_open());
}

void the_mute_shortcut_is_picked_from_its_list_and_says_when_it_is_taken() {
  Fixture f;
  f.show_end();
  CHECK(f.prefs.get(pref::mute_hotkey).empty());
  CHECK(f.h.click(f.tab, SoundTab::HotkeyList));
  CHECK(f.h.click(f.tab, SoundTab::HotkeyList + 3));
  CHECK(f.prefs.get(pref::mute_hotkey) == "ctrl_shift_m");
  const MuteHotkey* key = mute_hotkey_named(f.prefs.get(pref::mute_hotkey));
  CHECK(key && key->vk == 'M' && key->modifiers == (2u | 4u));
  // The shell found another program holding it: the row says so, and the panel grows by a line.
  const float before = f.tab.content_height();
  f.tab.set_hotkey_taken(true);
  f.h.render();
  CHECK(f.tab.content_height() > before);
  // Another choice is unknown until tried, and none is never taken.
  CHECK(f.h.click(f.tab, SoundTab::HotkeyList));
  CHECK(f.h.click(f.tab, SoundTab::HotkeyList + 4));
  CHECK(f.prefs.get(pref::mute_hotkey) == "pause");
  CHECK(!f.tab.hotkey_taken());
  CHECK(mute_hotkey_named("") == nullptr);
  CHECK(mute_hotkey_named("ctrl_alt_x") == nullptr);
}

void a_tab_fading_out_keeps_its_edge_fades() {
  Fixture f;
  f.h.app.ui().animations = true;
  f.show_end();
  f.h.app.settings().select(SettingsPage::Display);
  CHECK(f.h.app.settings().scroll_offset() == 0);  // the new tab starts at its top, without a fade there
  CHECK(f.h.app.settings().left_fade_top() == edge_fade_px);
  f.h.render();
}

void a_pack_is_picked_from_its_list() {
  Fixture f;
  CHECK(!f.h.drawn(f.tab, SoundTab::KeyboardList + 1));
  CHECK(f.h.click(f.tab, SoundTab::KeyboardList));
  CHECK(f.h.click(f.tab, SoundTab::KeyboardList + 3));
  CHECK(f.prefs.get(pref::keyboard_sound) == "walnut");
  CHECK(!f.h.drawn(f.tab, SoundTab::KeyboardList + 1));
  CHECK(f.h.click(f.tab, SoundTab::MouseList));
  CHECK(f.h.click(f.tab, SoundTab::MouseList + 2));
  CHECK(f.prefs.get(pref::mouse_sound) == "rubber");
  CHECK(f.prefs.get(pref::keyboard_sound) == "walnut");
}

void one_list_is_open_at_a_time_and_escape_closes_it_first() {
  Fixture f;
  CHECK(f.h.click(f.tab, SoundTab::KeyboardList));
  CHECK(f.h.click(f.tab, SoundTab::SpeakerList));
  CHECK(f.h.drawn(f.tab, SoundTab::SpeakerList + 1));
  CHECK(!f.h.drawn(f.tab, SoundTab::KeyboardList + 1));
  f.h.key(VK_ESCAPE);
  CHECK(!f.h.drawn(f.tab, SoundTab::SpeakerList + 1));
  CHECK(f.h.app.settings().is_open());
  f.h.key(VK_ESCAPE);
  CHECK(!f.h.app.settings().is_open());
}

void a_click_elsewhere_closes_the_list_and_changes_nothing() {
  Fixture f;
  CHECK(f.h.click(f.tab, SoundTab::KeyboardList));
  CHECK(f.h.click(f.tab, SoundTab::SpeakerVolume));  // the list covers the toggles
  CHECK(!f.h.drawn(f.tab, SoundTab::KeyboardList + 1));
  CHECK(f.prefs.get(pref::keyboard_sound) == "plastic");
}

void a_speaker_is_saved_with_its_name() {
  Fixture f;
  int picked = 0;
  f.tab.on_speaker_picked = [&] { ++picked; };
  CHECK(f.tab.speaker_options().size() == 3);
  CHECK(f.tab.speaker_options()[0] == "기본 출력 장치");
  CHECK(f.h.click(f.tab, SoundTab::SpeakerList));
  CHECK(f.h.click(f.tab, SoundTab::SpeakerList + 3));
  CHECK(f.prefs.get(pref::speaker_device) == "id-b");
  CHECK(f.prefs.get(pref::speaker_device_name) == "스피커 B");
  CHECK(picked == 1);
  // Back to the default: both are cleared.
  CHECK(f.h.click(f.tab, SoundTab::SpeakerList));
  CHECK(f.h.click(f.tab, SoundTab::SpeakerList + 1));
  CHECK(f.prefs.get(pref::speaker_device).empty());
  CHECK(f.prefs.get(pref::speaker_device_name).empty());
  CHECK(picked == 2);
}

void a_saved_speaker_that_left_stays_on_the_list() {
  Fixture f;
  f.prefs.set(pref::speaker_device, "gone");
  f.prefs.set(pref::speaker_device_name, "헤드셋");
  f.tab.set_speakers(f.speakers);
  CHECK(f.tab.speaker_options().size() == 4);
  CHECK(f.tab.speaker_options()[1] == "헤드셋");
  // A device that arrives or leaves while the tab is up only changes the list.
  f.tab.set_speakers({{"gone", "헤드셋 (돌아옴)"}});
  CHECK(f.tab.speaker_options().size() == 2);
  CHECK(f.tab.speaker_options()[1] == "헤드셋 (돌아옴)");
  f.tab.set_speakers({});
  f.prefs.set(pref::speaker_device_name, "");
  f.tab.set_speakers({});
  CHECK(f.tab.speaker_options()[1] == "지금 고른 출력 장치");
  CHECK(f.prefs.get(pref::speaker_device) == "gone");
}

void a_list_low_in_the_view_opens_upward_and_stays_clickable() {
  Fixture f;
  f.h.app.settings().scroll_to(40);
  f.h.render();
  CHECK(f.h.click(f.tab, SoundTab::MouseList));
  CHECK(f.h.click(f.tab, SoundTab::MouseList + 3));
  CHECK(f.prefs.get(pref::mouse_sound) == "walnut");
}

void opening_the_tab_asks_for_its_lists() {
  Fixture f;
  int shown = 0;
  f.tab.on_shown = [&] { ++shown; };
  f.h.app.settings().select(SettingsPage::System);
  CHECK(shown == 0);
  f.h.app.settings().select(SettingsPage::Sound);
  CHECK(shown == 1);
}

void reset_shows_at_once() {
  Fixture f;
  f.prefs.set(pref::keyboard_sound, "walnut");
  f.h.render();
  f.prefs.reset();
  f.h.render();
  CHECK(f.prefs.get(pref::keyboard_sound) == "plastic");
  CHECK(f.h.click(f.tab, SoundTab::KeyboardList));
  CHECK(f.h.click(f.tab, SoundTab::KeyboardList + 1));  // already selected: no change
  CHECK(f.prefs.get(pref::keyboard_sound) == "plastic");
}

}  // namespace

int main() {
  the_controls_are_there_and_pen_is_hidden();
  sliders_change_their_settings_and_only_when_different();
  a_slider_shows_its_tip_on_hover_and_while_dragged();
  every_toggle_flips_its_setting();
  the_balance_runs_from_55_to_90_and_can_be_off();
  a_tab_fading_out_keeps_its_edge_fades();
  the_balance_is_said_in_words();
  the_balance_is_locked_without_the_surround();
  the_reverb_is_picked_from_its_list_and_stays_without_the_surround();
  the_mute_shortcut_is_picked_from_its_list_and_says_when_it_is_taken();
  a_pack_is_picked_from_its_list();
  one_list_is_open_at_a_time_and_escape_closes_it_first();
  a_click_elsewhere_closes_the_list_and_changes_nothing();
  a_speaker_is_saved_with_its_name();
  a_saved_speaker_that_left_stays_on_the_list();
  a_list_low_in_the_view_opens_upward_and_stays_clickable();
  opening_the_tab_asks_for_its_lists();
  reset_shows_at_once();
  return crpp_test_result();
}
