#include <ctime>

#include <optional>

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

void test_upstreams_name_for_cumulus_is_kept_in_its_keys() {
  CHECK(std::string(pref::theme.fallback) == "cloudroom");
  CHECK(theme_from_upstream("cloudroom") == "cumulus");
  CHECK(theme_from_upstream("pastel") == "pastel");
}

void test_escape_closes_the_dropdown_then_the_panel() {
  UiHarness h;
  CHECK(h.start());
  open(h, SettingsPage::Display);
  DisplayTab& tab = h.app.settings().display();
  CHECK(h.click(tab, DisplayTab::SkinThemeList));
  CHECK(h.drawn(tab, DisplayTab::SkinThemeList + 2));  // + 1 is the heading
  h.key(VK_ESCAPE);
  CHECK(h.app.settings().is_open());
  CHECK(!h.drawn(tab, DisplayTab::SkinThemeList + 2));  // + 1 is the heading
  h.key(VK_ESCAPE);
  CHECK(!h.app.settings().is_open());
}

void test_click_elsewhere_closes_the_dropdown() {
  UiHarness h;
  CHECK(h.start());
  open(h, SettingsPage::Display);
  DisplayTab& tab = h.app.settings().display();
  CHECK(h.click(tab, DisplayTab::SkinThemeList));
  CHECK(h.drawn(tab, DisplayTab::SkinThemeList + 2));  // + 1 is the heading
  CHECK(h.click(tab, DisplayTab::DarkModeList));  // closes the open list and opens its own
  CHECK(!h.drawn(tab, DisplayTab::SkinThemeList + 2));
  CHECK(h.drawn(tab, DisplayTab::DarkModeList + 1));
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
  h.app.mouse_up(left - 500, y);
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
  tab.set_sounds({{"01_cloud_chat", "cloud_chat", "메시지"}, {"02_cloud_enter", "cloud_enter", "누군가 들어올 때"}});
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

// A fingerprint is text to select, to compare with what its publisher says.
void test_a_fingerprint_can_be_selected_and_copied(const char* pack) {
  UiHarness h;
  CHECK(h.start(951, 692, pack));
  FakeClipboard clipboard;
  h.app.ui().clipboard = &clipboard;
  SystemTab& tab = h.app.settings().system();
  tab.set_repositories({{"https://crpp.kawala.in/", "0123456789abcdef", true}});
  open(h, SettingsPage::System);
  for (const Pane pane : h.app.open_panes()) h.on_pane(pane);  // a window of its own under Retro
  h.app.settings().scroll_to(250);
  h.render();
  const Ui::Hit* hit = h.app.ui().find_hit(&tab, SystemTab::Fingerprint);
  CHECK(hit != nullptr);
  if (!hit) return;
  const float y = hit->rect.y + hit->rect.h / 2;
  hold_clock_at(5000000);
  h.app.mouse_move(hit->rect.x + 1, y);
  h.app.mouse_down(hit->rect.x + 1, y, false);
  h.app.mouse_move(hit->rect.right() - 1, y);
  h.app.mouse_up(hit->rect.right() - 1, y);
  CHECK(tab.selected_text() == L"지문 0123 4567 89ab cdef");
  CHECK(h.app.key_down('C', true, false) && clipboard.held == L"지문 0123 4567 89ab cdef");
  hold_clock_at(0);
}

// As in a Windows message box, Ctrl+C copies the question, a fingerprint in it included.
void test_a_question_is_copied_whole() {
  UiHarness h;
  CHECK(h.start());
  FakeClipboard clipboard;
  h.app.ui().clipboard = &clipboard;
  h.app.ui().dialogs->show({"Trust this key?\nab12 cd34", "Add", "Cancel", false, nullptr, nullptr});
  h.render();
  CHECK(h.app.key_down('C', true, false));
  CHECK(clipboard.held == L"Trust this key?\nab12 cd34");
}

// What is typed goes to the shell as typed, on the button or Enter, a row's button names its row,
// and an address is a link to its Repository.
void test_a_repository_is_added_and_removed_by_the_shell() {
  UiHarness h;
  CHECK(h.start());
  SystemTab& tab = h.app.settings().system();
  tab.set_repositories({{"https://crpp.kawala.in/", "0123456789abcdef", true}, {"github:someone/skins", "1a2b3c4d5e6f7a8b", false}});
  open(h, SettingsPage::System);
  std::vector<std::string> added;
  std::vector<std::size_t> removed;
  tab.on_add_repository = [&](std::string address) { added.push_back(std::move(address)); };
  tab.on_remove_repository = [&](std::size_t index) { removed.push_back(index); };
  std::vector<std::string> opened;
  tab.on_open_repository = [&](const std::string& address) { opened.push_back(address); };
  tab.set_packs({{"Retro", "1.2 MB", true}, {"Cloud", "In use", false}});
  std::vector<std::size_t> packs_removed;
  tab.on_remove_pack = [&](std::size_t index) { packs_removed.push_back(index); };
  h.app.settings().scroll_to(250);
  h.render();
  CHECK(!h.drawn(tab, SystemTab::RemoveRepository));  // the official one stays
  CHECK(h.click(tab, SystemTab::RemoveRepository + 1));
  CHECK(removed == std::vector<std::size_t>{1});
  CHECK(tab.is_link(SystemTab::OpenRepository + 1));
  CHECK(h.click(tab, SystemTab::OpenRepository + 1));
  CHECK(opened == std::vector<std::string>{"github:someone/skins"});

  CHECK(h.click(tab, SystemTab::AddRepository));
  CHECK(added.empty());  // nothing typed
  CHECK(h.click(tab, SystemTab::Address));
  CHECK(h.app.ui().focus == &tab.address());
  for (const wchar_t c : std::wstring_view(L"github:a/b")) h.app.char_input(c, false);
  h.app.char_input(L'\r', false);
  CHECK(added == std::vector<std::string>{"github:a/b"});
  CHECK(h.click(tab, SystemTab::AddRepository));
  CHECK(added.size() == 2);
  // Not again while one is being checked.
  tab.set_adding({SystemTab::Adding::State::Checking, {}});
  CHECK(h.click(tab, SystemTab::AddRepository));
  CHECK(added.size() == 2);

  h.app.settings().scroll_to(480);
  h.render();
  CHECK(h.click(tab, SystemTab::RemovePack));
  CHECK(packs_removed == std::vector<std::size_t>{0});
  CHECK(!h.drawn(tab, SystemTab::RemovePack + 1));  // the one in use has none
}

void test_language_dropdown_saves_the_locale() {
  UiHarness h;
  CHECK(h.start());
  open(h, SettingsPage::Display);
  DisplayTab& tab = h.app.settings().display();
  Prefs& prefs = *h.app.ui().prefs;
  h.app.settings().scroll_to(10000);
  h.render();
  CHECK(prefs.get(pref::language).empty());  // follows the OS until someone chooses
  CHECK(h.click(tab, DisplayTab::LanguageList));
  CHECK(h.click(tab, DisplayTab::LanguageList + 3));  // Dropdown ids: button, then options
  CHECK(prefs.get(pref::language) == "en");
  h.render();
  CHECK(h.click(tab, DisplayTab::LanguageList));
  CHECK(h.click(tab, DisplayTab::LanguageList + 1));
  CHECK(prefs.get(pref::language).empty());
}

// Written line by line: wrapped any further, it would run over the next heading.
void test_the_detail_hint_keeps_three_lines_in_every_locale() {
  UiHarness h;
  CHECK(h.start());
  for (const auto& locale : crpp::locale::all_locales()) {
    const auto hint = h.app.ui().gfx.layout(widen(locale.strings->system_detail_hint), {.size = 14, .line_height = 20, .wrap = true}, 439, 10000);
    CHECK(hint.get() != nullptr);
    DWRITE_TEXT_METRICS metrics{};
    if (hint) hint->GetMetrics(&metrics);
    CHECK(metrics.lineCount == 3);
  }
}

void test_idle_dropdown_saves_minutes() {
  UiHarness h;
  CHECK(h.start());
  open(h, SettingsPage::Display);
  DisplayTab& tab = h.app.settings().display();
  Prefs& prefs = *h.app.ui().prefs;
  h.app.settings().scroll_to(10000);
  h.render();
  CHECK(h.click(tab, DisplayTab::IdleList));
  CHECK(h.click(tab, DisplayTab::IdleList + 3));
  CHECK(prefs.get(pref::idle_minutes) == 60);
  prefs.set(pref::idle_minutes, 7);  // not a choice: taken as 30 minutes, as upstream does
  h.render();
  CHECK(h.click(tab, DisplayTab::IdleList));
  CHECK(h.click(tab, DisplayTab::IdleList + 2));  // already selected, so nothing changes
  CHECK(prefs.get(pref::idle_minutes) == 7);
}

void test_release_dropdown_saves_seconds() {
  UiHarness h;
  CHECK(h.start());
  open(h, SettingsPage::System);
  SystemTab& tab = h.app.settings().system();
  Prefs& prefs = *h.app.ui().prefs;
  CHECK(prefs.get(pref::release_graphics_after) == 0);  // at once
  h.app.settings().scroll_to(10000);
  h.render();
  CHECK(h.click(tab, SystemTab::ReleaseList));
  CHECK(h.click(tab, SystemTab::ReleaseList + 4));
  CHECK(prefs.get(pref::release_graphics_after) == -1);  // never
  h.render();
  CHECK(h.click(tab, SystemTab::ReleaseList));
  CHECK(h.click(tab, SystemTab::ReleaseList + 2));
  CHECK(prefs.get(pref::release_graphics_after) == 10);
  prefs.set(pref::release_graphics_after, 30);  // not a choice: shown as the 10 s below it
  h.render();
  CHECK(h.click(tab, SystemTab::ReleaseList));
  CHECK(h.click(tab, SystemTab::ReleaseList + 2));  // already selected, so nothing changes
  CHECK(prefs.get(pref::release_graphics_after) == 30);
}

void test_reset_goes_through_its_dialog() {
  UiHarness h;
  CHECK(h.start());
  SystemTab& tab = h.app.settings().system();
  Prefs& prefs = *h.app.ui().prefs;
  prefs.set(pref::use_24h, true);
  prefs.set(pref::input_detail, true);
  prefs.set(pref::theme, "pastel");
  prefs.set(pref::hidden_themes, true);
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
  CHECK(prefs.get(pref::hidden_themes));  // found, as an Achievement is reached: not a setting
}

void test_scrollbar_shows_on_scrolling_and_hides_after_a_pause() {
  UiHarness h;
  CHECK(h.start());
  open(h, SettingsPage::Display);
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
  open(h, SettingsPage::Display);
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

void test_chat_clock() {
  const long long ms = static_cast<long long>(std::time(nullptr)) * 1000;
  const std::string twelve = chat_clock_text(ms, false), full = chat_clock_text(ms, true);
  CHECK(twelve.find("오전") != std::string::npos || twelve.find("오후") != std::string::npos);
  CHECK(full.size() == 5);
  CHECK(full[2] == ':');
}

}  // namespace

int main() {
  test_a_fingerprint_can_be_selected_and_copied(CRPP_TEST_SKINS "/cloud");
  test_a_fingerprint_can_be_selected_and_copied(CRPP_TEST_SKINS "/retro");
  test_a_question_is_copied_whole();
  test_upstreams_name_for_cumulus_is_kept_in_its_keys();
  test_a_repository_is_added_and_removed_by_the_shell();
  test_escape_closes_the_dropdown_then_the_panel();
  test_click_elsewhere_closes_the_dropdown();
  test_slider_changes_the_chat_scale_within_its_range();
  test_notification_ids();
  test_notify_toggles_and_preview();
  test_system_rows();
  test_language_dropdown_saves_the_locale();
  test_the_detail_hint_keeps_three_lines_in_every_locale();
  test_idle_dropdown_saves_minutes();
  test_release_dropdown_saves_seconds();
  test_reset_goes_through_its_dialog();
  test_scrollbar_shows_on_scrolling_and_hides_after_a_pause();
  test_scrollbar_stays_under_the_pointer();
  test_a_tab_that_fits_has_no_scrollbar();
  test_chat_clock();
  return crpp_test_result();
}
