#include <dwmapi.h>

#include <algorithm>

#include "areas.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui::area::preferences {

namespace {

// Microsoft's documented way to read this is WinRT's UISettings; this value is the
// plain-Win32 equivalent Windows keeps it in. Missing means an older Windows: light.
bool system_prefers_dark() {
  DWORD light = 1, size = sizeof light;
  const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                                      L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
  return status == ERROR_SUCCESS && light == 0;
}

void apply_theme(Shell& s) {
  const ThemeId id = effective_theme(*s.app.ui().prefs, system_prefers_dark());
  s.app.set_theme(id);
  if (!s.hwnd) return;
  // The title bar too (DWMWA_USE_IMMERSIVE_DARK_MODE). Older Windows refuses; harmless.
  const BOOL dark = s.app.ui().theme.dark ? TRUE : FALSE;
  constexpr DWORD use_immersive_dark_mode = 20;
  DwmSetWindowAttribute(s.hwnd, use_immersive_dark_mode, &dark, sizeof dark);
  InvalidateRect(s.hwnd, nullptr, FALSE);
}

void apply_chat(Shell& s) {
  const Prefs& prefs = *s.app.ui().prefs;
  ChatPanel& chat = s.app.room().chat();
  chat.set_scale(std::clamp(prefs.get(pref::chat_scale), 80, 150));
  chat.set_use_24h(prefs.get(pref::use_24h));
}

void wire_tabs(Shell& s) {
  Shell* sp = &s;
  SettingsPanel& panel = s.app.settings();
  panel.display().on_shown = [sp] {
    sp->app.settings().display().set_sample(sp->store->string_or("nickname", ""), sp->store->string_or("emoji", ""));
  };
}

}  // namespace

void wire(Shell& s) {
  wire_tabs(s);
  apply_theme(s);
  apply_chat(s);
}

bool scene(Shell& s, std::string_view name) {
  Prefs& prefs = *s.app.ui().prefs;
  const auto dark = [&] { s.app.set_theme(ThemeId::DarkCloud); };
  const auto pastel = [&] { s.app.set_theme(ThemeId::Pastel); };
  SettingsPanel& panel = s.app.settings();
  const auto open = [&](SettingsPage page) {
    panel.open(page);
    panel.set_version("v0.0.0");
    panel.display().set_sample("크롬이", "1f600");
  };
  if (name == "settings") {
    panel.open(SettingsPage::Sound);
    panel.set_version("v0.0.0");
    return true;
  }
  if (name == "home-dark" || name == "home-pastel") {
    name == "home-dark" ? dark() : pastel();
    return true;
  }
  if (name == "room-dark" || name == "room-pastel") {
    name == "room-dark" ? dark() : pastel();
    return room::scene(s, "chat");
  }
  if (name == "room-big") {  // the largest chat size, on a 24-hour clock
    s.app.room().chat().set_scale(150);
    s.app.room().chat().set_use_24h(true);
    return room::scene(s, "chat");
  }
  if (name == "home-droplet-tip") {
    s.app.ui().hover_owner = &panel;
    s.app.ui().hover_id = SettingsPanel::Droplet;
    return true;
  }
  // settings-<tab>[-big][-on][-end][-tip][-dark|-pastel]: -big is the largest chat size,
  // -on turns on the option that changes the tab's rows, -end scrolls down, -tip shows
  // the tab's own tip or open list.
  if (!name.starts_with("settings-")) return false;
  std::string_view rest = name.substr(9);
  const auto suffix = [&](std::string_view tail) {
    if (!rest.ends_with(tail)) return false;
    rest.remove_suffix(tail.size());
    return true;
  };
  if (suffix("-dark")) dark();
  if (suffix("-pastel")) pastel();
  const bool tip = suffix("-tip");
  const bool to_end = suffix("-end");
  const bool on = suffix("-on");
  const bool big = suffix("-big");
  if (rest == "display") {
    open(SettingsPage::Display);
    if (on) prefs.set(pref::system_theme, true);
    if (big) prefs.set(pref::chat_scale, 150);
    if (tip) panel.display().click(on ? DisplayTab::LightList : DisplayTab::ThemeList, 0, 0, false);
  } else {
    return false;
  }
  if (to_end) panel.scroll_to(10000);
  return true;
}

void pref_changed(Shell& s, std::string_view key) {
  if (key == "theme" || key == "systemTheme" || key == "lightTheme" || key == "darkTheme") {
    apply_theme(s);
  } else if (key == "chatScale" || key == "use24h") {
    apply_chat(s);
    InvalidateRect(s.hwnd, nullptr, FALSE);
  }
  // A changed setting is written like a burst of chat, without waiting for the exit.
  if (!s.save_timer_on) {
    SetTimer(s.hwnd, save_timer, save_delay_ms, nullptr);
    s.save_timer_on = true;
  }
}

bool notification_enabled(Shell&, std::string_view) { return true; }

void system_settings_changed(Shell& s) {
  if (s.prefs && s.app.ui().prefs->get(pref::system_theme)) apply_theme(s);
}

}  // namespace crpp::gui::area::preferences
