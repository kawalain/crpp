#include <ctime>

#include "check.hpp"
#include "crpp/ui/controls.hpp"
#include "ui_harness.hpp"

using namespace crpp::ui;
using test::UiHarness;

namespace {

void open(UiHarness& h, SettingsPage page) {
  h.app.settings().open(page);
  h.render();
}

void test_effective_theme() {
  MemoryPrefs prefs;
  CHECK(effective_theme(prefs, false) == ThemeId::Cloudroom);
  CHECK(effective_theme(prefs, true) == ThemeId::Cloudroom);  // the OS only counts when linked
  prefs.set(pref::theme, "pastel");
  CHECK(effective_theme(prefs, true) == ThemeId::Pastel);
  prefs.set(pref::theme, "nonsense");
  CHECK(effective_theme(prefs, false) == ThemeId::Cloudroom);

  prefs.set(pref::system_theme, true);
  prefs.set(pref::light_theme, "pastel");
  CHECK(effective_theme(prefs, false) == ThemeId::Pastel);
  CHECK(effective_theme(prefs, true) == ThemeId::DarkCloud);
  prefs.set(pref::light_theme, "darkcloud");  // not a light theme
  CHECK(effective_theme(prefs, false) == ThemeId::Cloudroom);
}

void test_system_toggle_swaps_the_dropdowns() {
  UiHarness h;
  CHECK(h.start());
  open(h, SettingsPage::Display);
  DisplayTab& tab = h.app.settings().display();
  CHECK(h.drawn(tab, DisplayTab::ThemeList));
  CHECK(!h.drawn(tab, DisplayTab::LightList));
  const float short_height = tab.content_height();

  CHECK(h.click(tab, DisplayTab::SystemTheme));
  CHECK(h.app.ui().prefs->get(pref::system_theme));
  CHECK(!h.drawn(tab, DisplayTab::ThemeList));
  CHECK(h.drawn(tab, DisplayTab::LightList));
  CHECK(h.drawn(tab, DisplayTab::DarkList));
  CHECK(tab.content_height() == short_height + 52);
}

void test_theme_dropdown_saves_upstreams_id() {
  UiHarness h;
  CHECK(h.start());
  open(h, SettingsPage::Display);
  DisplayTab& tab = h.app.settings().display();
  CHECK(!h.drawn(tab, DisplayTab::ThemeList + 2));
  CHECK(h.click(tab, DisplayTab::ThemeList));
  CHECK(h.drawn(tab, DisplayTab::ThemeList + 2));
  CHECK(h.click(tab, DisplayTab::ThemeList + 2));  // 먹구름
  CHECK(h.app.ui().prefs->get(pref::theme) == "darkcloud");
  CHECK(!h.drawn(tab, DisplayTab::ThemeList + 2));
}

void test_escape_closes_the_dropdown_then_the_panel() {
  UiHarness h;
  CHECK(h.start());
  open(h, SettingsPage::Display);
  DisplayTab& tab = h.app.settings().display();
  CHECK(h.click(tab, DisplayTab::ThemeList));
  h.key(VK_ESCAPE);
  CHECK(h.app.settings().is_open());
  CHECK(!h.drawn(tab, DisplayTab::ThemeList + 1));
  h.key(VK_ESCAPE);
  CHECK(!h.app.settings().is_open());
}

void test_click_elsewhere_closes_the_dropdown() {
  UiHarness h;
  CHECK(h.start());
  open(h, SettingsPage::Display);
  DisplayTab& tab = h.app.settings().display();
  CHECK(h.click(tab, DisplayTab::ThemeList));
  CHECK(h.click(tab, DisplayTab::SystemTheme));
  h.render();
  CHECK(!h.drawn(tab, DisplayTab::LightList + 1));
  CHECK(h.click(tab, DisplayTab::SystemTheme));
  CHECK(!h.drawn(tab, DisplayTab::ThemeList + 1));
}

void test_slider_changes_the_chat_scale_within_its_range() {
  UiHarness h;
  CHECK(h.start());
  open(h, SettingsPage::Display);
  DisplayTab& tab = h.app.settings().display();
  const Ui::Hit* hit = h.app.ui().find_hit(&tab, DisplayTab::ScaleSlider);
  CHECK(hit != nullptr);
  if (!hit) return;
  const float y = hit->rect.y + hit->rect.h / 2, left = hit->rect.x, right = hit->rect.right();
  h.app.mouse_move(left + 30, y);
  h.app.mouse_down(left + 30, y, false);
  CHECK(h.app.ui().capture == &tab);
  h.app.mouse_move(right + 500, y);
  CHECK(h.app.ui().prefs->get(pref::chat_scale) == 150);
  h.app.mouse_move(left - 500, y);
  CHECK(h.app.ui().prefs->get(pref::chat_scale) == 80);
  h.app.mouse_up();
  CHECK(h.app.ui().capture == nullptr);
}

void test_notification_ids() {
  CHECK(pref::notification_id("01_cloud_chat") == "cloud_chat");
  CHECK(pref::notification_id("02_Cloud_Enter") == "cloud_enter");
  CHECK(pref::notification_id("cloud_chat") == "cloud_chat");
  CHECK(pref::notification_id("7up") == "7up");
}

void test_notify_toggles_and_preview() {
  UiHarness h;
  CHECK(h.start());
  NotifyTab& tab = h.app.settings().notify();
  tab.set_sounds({{"01_cloud_chat", "cloud_chat", "메시지"}, {"02_cloud_enter", "cloud_enter", "구름 입장"}});
  std::string previewed;
  tab.on_preview = [&](const std::string& stem) { previewed = stem; };
  open(h, SettingsPage::Notify);
  const Prefs& prefs = *h.app.ui().prefs;

  CHECK(prefs.notification_sound("cloud_enter"));
  CHECK(h.click(tab, NotifyTab::SoundBase + 1));
  CHECK(!prefs.notification_sound("cloud_enter"));
  CHECK(prefs.notification_sound("cloud_chat"));

  CHECK(h.click(tab, NotifyTab::PreviewBase));
  CHECK(previewed == "01_cloud_chat");
  CHECK(prefs.notification_sound("cloud_chat"));  // a preview changes nothing

  CHECK(prefs.get(pref::chat_tray_notify));
  CHECK(h.click(tab, NotifyTab::TrayToggle));
  CHECK(!prefs.get(pref::chat_tray_notify));
  CHECK(h.click(tab, NotifyTab::FlashToggle));
  CHECK(!prefs.get(pref::chat_taskbar_flash));
}

void test_system_rows() {
  UiHarness h;
  CHECK(h.start());
  open(h, SettingsPage::System);
  SystemTab& tab = h.app.settings().system();
  Prefs& prefs = *h.app.ui().prefs;

  CHECK(h.click(tab, SystemTab::Autostart));
  CHECK(prefs.get(pref::start_with_system));
  CHECK(h.click(tab, SystemTab::Tray));
  CHECK(!prefs.get(pref::minimize_to_tray));
  // Written line by line: wrapped any further, it would run over the next heading.
  const auto hint = h.app.ui().gfx.layout(widen(SystemTab::detail_hint), {.size = 14, .line_height = 20, .wrap = true}, 439, 10000);
  CHECK(hint.get() != nullptr);
  DWRITE_TEXT_METRICS metrics{};
  if (hint) hint->GetMetrics(&metrics);
  CHECK(metrics.lineCount == 3);
  CHECK(!prefs.get(pref::input_detail));  // Presence mode is the default
  CHECK(h.click(tab, SystemTab::Detail));
  CHECK(prefs.get(pref::input_detail));

  h.app.settings().scroll_to(10000);
  h.render();
  CHECK(!h.drawn(tab, SystemTab::OpenLogs));
  CHECK(h.click(tab, SystemTab::Logging));
  h.render();
  CHECK(prefs.get(pref::log_enabled));
  CHECK(h.drawn(tab, SystemTab::OpenLogs));
  int opened = 0;
  tab.on_open_logs = [&] { ++opened; };
  h.app.settings().scroll_to(10000);
  h.render();
  CHECK(h.click(tab, SystemTab::OpenLogs));
  CHECK(opened == 1);
}

void test_idle_dropdown_saves_minutes() {
  UiHarness h;
  CHECK(h.start());
  open(h, SettingsPage::System);
  SystemTab& tab = h.app.settings().system();
  Prefs& prefs = *h.app.ui().prefs;
  h.app.settings().scroll_to(150);
  h.render();
  CHECK(h.click(tab, SystemTab::IdleList));
  CHECK(h.click(tab, SystemTab::IdleList + 3));
  CHECK(prefs.get(pref::idle_minutes) == 60);
  prefs.set(pref::idle_minutes, 7);  // not a choice: taken as 30 minutes, as upstream does
  h.render();
  CHECK(h.click(tab, SystemTab::IdleList));
  CHECK(h.click(tab, SystemTab::IdleList + 2));  // already selected, so nothing changes
  CHECK(prefs.get(pref::idle_minutes) == 7);
}

void test_reset_goes_through_its_dialog() {
  UiHarness h;
  CHECK(h.start());
  SystemTab& tab = h.app.settings().system();
  Prefs& prefs = *h.app.ui().prefs;
  prefs.set(pref::use_24h, true);
  prefs.set(pref::input_detail, true);
  prefs.set(pref::theme, "pastel");
  prefs.set_notification_sound("cloud_chat", false);
  open(h, SettingsPage::System);
  h.app.settings().scroll_to(10000);
  h.render();

  CHECK(h.click(tab, SystemTab::Reset));
  CHECK(h.app.dialogs().is_open());
  h.key(VK_ESCAPE);  // the dialog goes first, and the panel stays
  CHECK(!h.app.dialogs().is_open());
  CHECK(h.app.settings().is_open());
  CHECK(prefs.get(pref::use_24h));

  CHECK(h.click(tab, SystemTab::Reset));
  CHECK(h.click(h.app.dialogs(), Dialogs::Confirm));
  CHECK(!prefs.get(pref::use_24h));
  CHECK(!prefs.get(pref::input_detail));
  CHECK(prefs.get(pref::theme) == "cloudroom");
  CHECK(prefs.notification_sound("cloud_chat"));
}

void test_scrollbar_shows_on_scrolling_and_hides_after_a_pause() {
  UiHarness h;
  CHECK(h.start());
  open(h, SettingsPage::System);
  SettingsPanel& panel = h.app.settings();
  CHECK(!panel.scrollbar_shown());
  CHECK(panel.wake_at() == 0);

  h.app.wheel(500, 300, -1);
  h.render();
  CHECK(panel.scroll_offset() > 0);
  CHECK(panel.scrollbar_shown());
  const auto wake = panel.wake_at();  // waits without needing frames
  CHECK(wake != 0);
  CHECK(!panel.animating());
  CHECK(h.app.tick(wake));
  CHECK(!panel.scrollbar_shown());
  h.render();
  CHECK(panel.wake_at() == 0);
}

void test_scrollbar_stays_under_the_pointer() {
  UiHarness h;
  CHECK(h.start());
  open(h, SettingsPage::System);
  SettingsPanel& panel = h.app.settings();
  const Ui::Hit* track = h.app.ui().find_hit(&panel, SettingsPanel::Track);
  CHECK(track != nullptr);
  if (!track) return;
  const float x = track->rect.x + 4, y = track->rect.bottom() - 2;
  h.app.mouse_move(x, y);
  h.render();
  CHECK(panel.scrollbar_shown());
  CHECK(panel.wake_at() == 0);
  h.app.mouse_move(10, 10);
  h.render();
  CHECK(panel.wake_at() != 0);
}

void test_a_tab_that_fits_has_no_scrollbar() {
  UiHarness h;
  CHECK(h.start());
  open(h, SettingsPage::Notify);
  SettingsPanel& panel = h.app.settings();
  CHECK(!h.drawn(panel, SettingsPanel::Track));
  h.app.wheel(500, 300, -1);
  h.render();
  CHECK(panel.scroll_offset() == 0);
  CHECK(!panel.scrollbar_shown());
}

void test_fade_runs_only_when_animated() {
  Ui ui;
  ui.animations = true;
  ui.now_ms = 1000;
  Fade fade;
  fade.set(ui, false, 100);
  CHECK(!fade.running(ui));  // the first state does not animate
  CHECK(fade.value(ui) == 0);
  fade.set(ui, true, 100);
  CHECK(fade.running(ui));
  ui.now_ms = 1050;
  const float middle = fade.value(ui);
  CHECK(middle > 0 && middle < 1);
  fade.set(ui, false, 100);  // turning back starts from where it is
  CHECK(fade.value(ui) == middle);
  ui.now_ms = 1200;
  CHECK(!fade.running(ui));
  CHECK(fade.value(ui) == 0);
}

void test_chat_clock() {
  const long long ms = static_cast<long long>(std::time(nullptr)) * 1000;
  const std::string twelve = chat_clock_text(ms, false), full = chat_clock_text(ms, true);
  CHECK(twelve.find("오전") != std::string::npos || twelve.find("오후") != std::string::npos);
  CHECK(full.size() == 5);
  CHECK(full[2] == ':');
}

}  // namespace

int main() {
  test_effective_theme();
  test_system_toggle_swaps_the_dropdowns();
  test_theme_dropdown_saves_upstreams_id();
  test_escape_closes_the_dropdown_then_the_panel();
  test_click_elsewhere_closes_the_dropdown();
  test_slider_changes_the_chat_scale_within_its_range();
  test_notification_ids();
  test_notify_toggles_and_preview();
  test_system_rows();
  test_idle_dropdown_saves_minutes();
  test_reset_goes_through_its_dialog();
  test_scrollbar_shows_on_scrolling_and_hides_after_a_pause();
  test_scrollbar_stays_under_the_pointer();
  test_a_tab_that_fits_has_no_scrollbar();
  test_fade_runs_only_when_animated();
  test_chat_clock();
  return crpp_test_result();
}
