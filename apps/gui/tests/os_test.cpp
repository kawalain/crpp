// The OS integration's decisions, and the tray icon's picture. Nothing here reaches the
// taskbar or the registry.
#include <cstdint>
#include <string>
#include <vector>

#include "autostart.hpp"
#include "check.hpp"
#include "crpp/ui/gfx.hpp"
#include "os_policy.hpp"
#include "tray.hpp"

using namespace crpp::gui;
using namespace crpp::gui::os_policy;

namespace {

void close_and_start() {
  CHECK(close_hides(true, true));
  CHECK(!close_hides(false, true));
  CHECK(!close_hides(true, false));  // no icon, no way back: exit
  CHECK(!close_hides(false, false));
  CHECK(start_hidden(true, true));
  CHECK(!start_hidden(true, false));
  CHECK(!start_hidden(false, true));
}

void only_an_official_build_shares_the_machine() {
  CHECK(shares_the_machine(true, false));
  CHECK(!shares_the_machine(false, false));
  CHECK(!shares_the_machine(true, true));
  CHECK(!shares_the_machine(false, true));
}

void a_development_build_keeps_its_data_beside_it() {
  CHECK(data_beside_exe(false, false));
  CHECK(!data_beside_exe(false, true));  // --data wins
  CHECK(!data_beside_exe(true, false));
  CHECK(!data_beside_exe(true, true));
}

void a_development_build_has_its_own_window() {
  CHECK(std::wstring_view(window_class(true)) != window_class(false));
  CHECK(std::wstring_view(window_title(true)) == L"Cloudroom++");
  CHECK(std::wstring_view(window_title(false)) == L"Cloudroom++ (dev)");
}

void hint_appears_once_in_a_room() {
  CHECK(hide_needs_hint(true, false));
  CHECK(!hide_needs_hint(true, true));
  CHECK(!hide_needs_hint(false, false));
}

void hidden_means_not_on_screen() {
  CHECK(!window_hidden(true, false));
  CHECK(window_hidden(false, false));
  CHECK(window_hidden(true, true));
}

void chat_on_a_focused_window_only_sounds() {
  const auto r = chat_reaction({.hidden = false, .focused = true});
  CHECK(r.sound);
  CHECK(!r.notify);
  CHECK(!r.flash);
}

void chat_while_away() {
  for (const bool hidden : {false, true}) {
    const auto r = chat_reaction({.hidden = hidden, .focused = false});
    CHECK(r.sound);
    CHECK(r.notify);
    CHECK(r.flash);
  }
  // A hidden window cannot be the focused one, whatever was last reported.
  CHECK(chat_reaction({.hidden = true, .focused = true}).notify);
}

void chat_settings_are_honored() {
  auto r = chat_reaction({.focused = false, .tray_notify = false});
  CHECK(!r.notify);
  CHECK(r.flash);
  r = chat_reaction({.focused = false, .taskbar_flash = false});
  CHECK(r.notify);
  CHECK(!r.flash);
}

void my_own_chat_does_nothing() {
  const auto r = chat_reaction({.mine = true, .hidden = true});
  CHECK(!r.sound);
  CHECK(!r.notify);
  CHECK(!r.flash);
}

void a_muted_sender_still_flashes() {  // as upstream: the message is still unread
  const auto r = chat_reaction({.muted = true, .focused = false});
  CHECK(!r.sound);
  CHECK(!r.notify);
  CHECK(r.flash);
}

void one_flash_until_seen() {
  CHECK(!chat_reaction({.focused = false, .flashing = true}).flash);
  CHECK(chat_reaction({.focused = false, .flashing = true}).notify);
}

void members_entering() {
  const std::vector<std::string> known{"a", "me"};
  const std::uint32_t me = 1;
  CHECK(someone_entered(known, {{"a", 0}, {"me", 1}, {"b", 2}}, me));
  CHECK(!someone_entered(known, {{"a", 0}, {"me", 1}}, me));
  CHECK(!someone_entered(known, {{"a", 0}}, me));                       // leaving is silent
  CHECK(!someone_entered(known, {{"a", 0}, {"new-me", 1}}, me));        // me, under a new id
  CHECK(!someone_entered(known, {{"b", 2, true}}, me));                 // back from a reconnect
  CHECK(!someone_entered(known, {{"b", 2, false, true}}, me));          // muted
  CHECK(!someone_entered({}, {{"a", 0}, {"b", 2}}, me));                // the first list of a Room
  CHECK(someone_entered(known, {{"b", 2, true}, {"c", 3}}, me));        // one of several
  CHECK(member_id("cid", 3) == "cid");
  CHECK(member_id("", 3) == "slot-3");
}

void text_is_cut_whole() {
  CHECK(fit_utf16(L"abc", 8) == 3);
  CHECK(fit_utf16(L"abc", 4) == 3);
  CHECK(fit_utf16(L"abc", 3) == 2);
  CHECK(fit_utf16(L"abc", 0) == 0);
  CHECK(fit_utf16(L"a\xD83D\xDE00z", 3) == 1);  // would end inside the pair
  CHECK(fit_utf16(L"a\xD83D\xDE00z", 4) == 3);
  CHECK(fit_utf16(L"a\xD83D\xDE00", 4) == 3);
}

void autostart_follows_the_setting() {
  MemoryRunKey key;
  CHECK(apply_autostart(key, true, L"C:\\Program Files\\crpp.exe").has_value());
  CHECK(key.value == L"\"C:\\Program Files\\crpp.exe\" --minimized");
  CHECK(key.writes == 1);
  CHECK(apply_autostart(key, true, L"C:\\Program Files\\crpp.exe").has_value());
  CHECK(key.writes == 1);  // already so
  CHECK(apply_autostart(key, true, L"D:\\crpp.exe").has_value());
  CHECK(key.value == L"\"D:\\crpp.exe\" --minimized");
  CHECK(apply_autostart(key, false, L"D:\\crpp.exe").has_value());
  CHECK(!key.value);
  CHECK(apply_autostart(key, false, L"D:\\crpp.exe").has_value());  // already gone
}

void autostart_refuses_a_command_that_would_not_run() {
  MemoryRunKey key;
  // 2 quotes and " --minimized" leave 246 of the 260 characters.
  CHECK(apply_autostart(key, true, std::wstring(246, L'x')).has_value());
  key.value.reset();
  const auto result = apply_autostart(key, true, std::wstring(247, L'x'));
  CHECK(!result && result.error() == AutostartError::TooLong);
  CHECK(!key.value);
}

class FailingRunKey final : public RunKey {
 public:
  std::expected<std::optional<std::wstring>, AutostartError> read() override { return std::unexpected(AutostartError::Registry); }
  std::expected<void, AutostartError> write(std::wstring_view) override { return std::unexpected(AutostartError::Registry); }
  std::expected<void, AutostartError> remove() override { return std::unexpected(AutostartError::Registry); }
};

void autostart_reports_a_failing_registry() {
  FailingRunKey key;
  CHECK(!apply_autostart(key, true, L"a.exe"));
  CHECK(!apply_autostart(key, false, L"a.exe"));
}

void pixels_become_an_icon() {
  crpp::ui::Gfx gfx;
  CHECK(gfx.init());
  const int size = 16;
  const auto pixels = render_pixels(gfx.d2d(), size, [](ID2D1RenderTarget* rt) { rt->Clear(D2D1::ColorF(0x5DADEC)); });
  CHECK(pixels.size() == static_cast<std::size_t>(size * size));
  HICON icon = icon_from_pixels(pixels, size);
  CHECK(icon != nullptr);
  if (icon) DestroyIcon(icon);
  CHECK(icon_from_pixels({}, 16) == nullptr);
}

}  // namespace

int main() {
  close_and_start();
  only_an_official_build_shares_the_machine();
  a_development_build_keeps_its_data_beside_it();
  a_development_build_has_its_own_window();
  hint_appears_once_in_a_room();
  hidden_means_not_on_screen();
  chat_on_a_focused_window_only_sounds();
  chat_while_away();
  chat_settings_are_honored();
  my_own_chat_does_nothing();
  a_muted_sender_still_flashes();
  one_flash_until_seen();
  members_entering();
  text_is_cut_whole();
  autostart_follows_the_setting();
  autostart_refuses_a_command_that_would_not_run();
  autostart_reports_a_failing_registry();
  pixels_become_an_icon();
  return crpp_test_result();
}
