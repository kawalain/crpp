#include <dwmapi.h>
#include <shellapi.h>

#include "areas.hpp"
#include "notification_sounds.hpp"
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
  s.app.set_theme(effective_theme(*s.app.ui().prefs, system_prefers_dark()).id);
  area::sound::theme_changed(s);
  if (!s.hwnd) return;
  // The title bar too (DWMWA_USE_IMMERSIVE_DARK_MODE). Older Windows refuses; harmless.
  const BOOL dark = s.app.ui().theme.dark ? TRUE : FALSE;
  constexpr DWORD use_immersive_dark_mode = 20;
  DwmSetWindowAttribute(s.hwnd, use_immersive_dark_mode, &dark, sizeof dark);
  frame_changed(s);
  InvalidateRect(s.hwnd, nullptr, FALSE);
}

// Windows' "Animation effects", as a browser's prefers-reduced-motion follows it.
void follow_system_animations(Shell& s) {
  BOOL on = TRUE;
  if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0)) s.app.set_animations(on != FALSE);
}

// In Windows' display language, "ko-KR".
std::string os_language() {
  wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
  if (LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0) == 0) return {};
  return ui::narrow(name);
}

void apply_chat(Shell& s) {
  const Prefs& prefs = *s.app.ui().prefs;
  ChatPanel& chat = s.app.room().chat();
  chat.set_scale(std::clamp(prefs.get(pref::chat_scale), 80, 150));
  chat.set_use_24h(prefs.get(pref::use_24h));
}

std::vector<NotifySound> load_sounds(const Shell& s) {
  constexpr std::string_view folder = "notifications/", wav = ".wav";
  std::vector<std::string> stems;
  for (const std::string& path : s.expected_sounds) {
    if (path.starts_with(folder) && path.ends_with(wav)) stems.push_back(path.substr(folder.size(), path.size() - folder.size() - wav.size()));
  }
  std::vector<NotifySound> sounds;
  for (auto& found : find_notification_sounds(s.assets_dir / "sounds" / "notifications", pref::notification_id, stems)) {
    // Those this program has words for; a pack.json names only what it brings.
    if (found.id == "cloud_chat") found.name = tr().notify_sound_chat;
    if (found.id == "cloud_enter") found.name = tr().notify_sound_enter;
    sounds.push_back({std::move(found.stem), std::move(found.id), std::move(found.name)});
  }
  return sounds;
}

void open_log_folder(Shell& s) {
  const std::filesystem::path folder = s.log.file().parent_path();
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  // ShellExecute's result above 32 is success.
  const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(s.hwnd, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  if (result <= 32) s.app.toasts().toast(tr().system_logs_failed);
}

DiagEvent event_of(session::State state) {
  switch (state) {
    case session::State::Connecting:
      return DiagEvent::Connecting;
    case session::State::Joining:
      return DiagEvent::Joining;
    case session::State::InRoom:
      return DiagEvent::InRoom;
    case session::State::Leaving:
      return DiagEvent::Leaving;
    case session::State::Waiting:
      return DiagEvent::WaitingToReconnect;
    default:
      return DiagEvent::ConnectionIdle;
  }
}

DiagEvent event_of(session::Failure failure) {
  switch (failure) {
    case session::Failure::Unauthorized:
      return DiagEvent::FailedUnauthorized;
    case session::Failure::ConnectFailed:
      return DiagEvent::FailedToConnect;
    case session::Failure::RoomGone:
      return DiagEvent::FailedRoomGone;
    case session::Failure::ServerError:
      return DiagEvent::FailedServerError;
    default:
      return DiagEvent::FailedInvalidArgument;
  }
}

// Notes what the connection does, after whoever already listens.
void log_connection(Shell& s) {
  Shell* sp = &s;
  s.bridge->on_state = [sp, next = std::move(s.bridge->on_state)](session::State state) {
    sp->log.note(event_of(state));
    if (next) next(state);
  };
  // The detail is left out: it comes from the server, and the log takes no text.
  s.bridge->on_failure = [sp, next = std::move(s.bridge->on_failure)](session::Failure failure, const std::string& detail) {
    sp->log.note(event_of(failure));
    if (next) next(failure, detail);
  };
}

void wire_tabs(Shell& s) {
  Shell* sp = &s;
  SettingsPanel& panel = s.app.settings();
  panel.display().on_shown = [sp] {
    sp->app.settings().display().set_sample(sp->store->string_or("nickname", ""), sp->store->string_or("emoji", ""));
  };
  panel.notify().on_shown = [sp] { sp->app.settings().notify().set_sounds(load_sounds(*sp)); };
  panel.notify().on_preview = [sp](const std::string& stem) { sound::preview_notification(*sp, stem); };
  panel.system().on_open_logs = [sp] { open_log_folder(*sp); };
}

}  // namespace

void apply_language(Shell& s) {
  const locale::Locale& chosen = locale::choose_locale(s.app.ui().prefs->get(pref::language), os_language());
  locale::set_current(chosen);
  s.app.ui().gfx.set_language(ui::widen(chosen.tag));
}

void wire(Shell& s) {
  follow_system_animations(s);
  wire_tabs(s);
  log_connection(s);
  apply_theme(s);
  apply_chat(s);
  s.log.set_enabled(s.app.ui().prefs->get(pref::log_enabled));
}

bool scene(Shell& s, std::string_view name) {
  Prefs& prefs = *s.app.ui().prefs;
  const auto dark = [&] { s.app.set_theme("darkcloud"); };
  const auto pastel = [&] { s.app.set_theme("pastel"); };
  SettingsPanel& panel = s.app.settings();
  const auto open = [&](SettingsPage page) {
    panel.open(page);
    panel.set_version("v0.0.0");
    panel.notify().set_sounds(load_sounds(s));
    panel.display().set_sample("크롬이", "1f600");
  };
  if (name == "settings") return sound::scene(s, "settings-sound");
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
  } else if (rest == "notify") {
    open(SettingsPage::Notify);
    if (tip) {
      s.app.ui().hover_owner = &panel.notify();
      s.app.ui().hover_id = NotifyTab::PreviewBase;
    }
  } else if (rest == "system") {
    open(SettingsPage::System);
    if (on) prefs.set(pref::log_enabled, true);
    if (tip) {
      s.app.ui().hover_owner = &panel;
      s.app.ui().hover_id = SettingsPanel::Version;
    }
  } else {
    return false;
  }
  if (to_end) panel.scroll_to(10000);
  return true;
}

void pref_changed(Shell& s, std::string_view key) {
  if (key == "theme" || key == "systemTheme" || key == "lightTheme" || key == "darkTheme") {
    apply_theme(s);
  } else if (key == "language") {
    apply_language(s);
    s.app.room().chat().locale_changed();
    sound::language_changed(s);
    InvalidateRect(s.hwnd, nullptr, FALSE);
  } else if (key == "chatScale" || key == "use24h") {
    apply_chat(s);
    InvalidateRect(s.hwnd, nullptr, FALSE);
  } else if (key == "inputDetail") {
    s.input->set_privacy(s.app.ui().prefs->get(pref::input_detail) ? input::Privacy::Detail : input::Privacy::Presence);
  } else if (key == "logEnabled") {
    s.log.set_enabled(s.app.ui().prefs->get(pref::log_enabled));
  }
  // A changed setting is written like a burst of chat, without waiting for the exit.
  if (!s.save_timer_on) {
    SetTimer(s.hwnd, save_timer, save_delay_ms, nullptr);
    s.save_timer_on = true;
  }
}

bool notification_enabled(Shell& s, std::string_view name) {
  return s.app.ui().prefs->notification_sound(pref::notification_id(name));
}

void system_settings_changed(Shell& s) {
  follow_system_animations(s);
  if (s.prefs && s.app.ui().prefs->get(pref::system_theme)) apply_theme(s);
}

}  // namespace crpp::gui::area::preferences
