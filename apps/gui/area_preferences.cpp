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
  forget_cursors(s);
  if (!s.app.ui().skins.empty()) s.app.apply_skin_choice(system_prefers_dark());
  else s.app.set_theme(effective_theme(*s.app.ui().prefs, system_prefers_dark()).id);
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

void note_skipped(Shell& s, bool say) {
  const std::string chosen = s.app.ui().prefs ? s.app.ui().prefs->get(pref::skin) : std::string();
  for (const App::SkippedPack& skipped : s.app.skipped_packs()) {
    s.log.note(DiagEvent::SkinNotLoaded, skipped.id + ": " + skipped.why);
    if (say && skipped.id == chosen) s.app.toasts().toast(locale::fill(tr().skin_not_loaded, {skipped.id}));
  }
}

void skins_installed(Shell& s) {
  (void)s.app.find_skins(repositories::skin_folders(s));
  note_skipped(s, false);
  apply_theme(s);
  offer_packs(s);
  repositories::show_packs(s);
}

namespace {

// The packs of one Repository's catalog that are not installed, under the ids the display tab knows
// them by: with the Repository's digits before them, unless it is the official one. What they
// extend may be in another Repository; one not added yet is named, and the pack cannot be picked.
void offer_from(const Shell& s, std::span<const assets::Shelf> shelves, std::size_t shelf, std::vector<SkinInfo>& offered) {
  const auto& installed = s.app.ui().skins;
  for (const assets::SkinPack& pack : shelves[shelf].catalog->skins) {
    std::string id = repositories::full_id(s, shelf, pack.id);
    if (pack.format != skin::pack_format || std::ranges::any_of(installed, [&](const SkinInfo& info) { return info.id == id; })) continue;
    const assets::Chain chain = assets::pack_chain(shelves, 0, shelf, pack.id);
    if (chain.gap != assets::Chain::Gap::None && chain.gap != assets::Chain::Gap::Repository) continue;
    SkinInfo info{.id = std::move(id), .names = pack.names};
    // Its Themes, then those it inherits from what it extends.
    for (const assets::Chain::Link& link : chain.links) {
      const assets::Catalog& catalog = *shelves[link.shelf].catalog;
      for (const assets::CatalogTheme& theme : catalog.skin(link.pack)->themes) {
        if (std::ranges::any_of(info.themes, [&](const skin::PackTheme& t) { return t.id == theme.id; })) continue;
        info.themes.push_back({.id = theme.id, .dark = theme.dark ? "dark" : "", .names = theme.names});  // only whether it has one
      }
      if (!repositories::installs_into(s, link.shelf)) continue;
      const std::string group[] = {assets::skin_group(link.pack)};
      for (const assets::File* file : assets::missing(catalog, group, repositories::install_dir(s, link.shelf))) info.to_fetch += file->size;
    }
    if (chain.gap == assets::Chain::Gap::Repository) {
      const assets::ParentPin& wanted = *chain.wanted;
      info.needs = locale::fill(tr().display_needs_repository,
                                {wanted.repository.empty() ? wanted.fingerprint.substr(0, assets::short_fingerprint) : wanted.repository});
    }
    if (!info.themes.empty()) offered.push_back(std::move(info));
  }
}

}  // namespace

void offer_packs(Shell& s) {
  std::vector<SkinInfo>& offered = s.app.ui().offered;
  offered.clear();
  const auto shelves = repositories::shelves(s);
  // The official one offers from the latest catalog only, which a development exe does not read.
  for (std::size_t shelf = s.offered ? 0 : 1; shelf < shelves.size(); ++shelf) {
    if (shelves[shelf].catalog) offer_from(s, shelves, shelf, offered);
  }
}

namespace {

const char* reason_of(assets::Error error) {
  switch (error) {
    case assets::Error::Network: return tr().fetch_reason_network;
    case assets::Error::Unavailable: return tr().fetch_reason_unavailable;
    case assets::Error::BadContent:
    case assets::Error::BadSignature: return tr().fetch_reason_bad;
    case assets::Error::Disk: return tr().fetch_reason_disk;
  }
  return tr().fetch_reason_network;
}

void fetch(Shell& s, const std::string& skin, const std::string& theme) {
  ThemeFetch& f = s.app.ui().theme_fetch;
  if (!(f.state == ThemeFetch::State::Fetching && f.skin == skin)) f.percent = 0;
  f.skin = skin;
  f.theme = theme;
  f.apply = true;
  f.reason.clear();
  f.state = ThemeFetch::State::Fetching;
  if (!fetch_pack(s, skin)) {
    f.state = ThemeFetch::State::Failed;
    f.reason = tr().fetch_reason_unavailable;
  }
}

}  // namespace

void pack_progress(Shell& s, const std::string& skin, int percent) {
  ThemeFetch& f = s.app.ui().theme_fetch;
  if (f.state == ThemeFetch::State::Fetching && f.skin == skin) f.percent = std::max(f.percent, percent);
}

void pack_fetched(Shell& s, const std::string& skin, bool cancelled, const std::expected<void, assets::Error>& result) {
  ThemeFetch& f = s.app.ui().theme_fetch;
  const bool current = f.state == ThemeFetch::State::Fetching && f.skin == skin;
  // A cancelled one left the row already, and one picked again since has a job of its own; one
  // the user said no to before it began is still in it.
  if (cancelled) {
    if (current && f.percent == 0) f = {};
    offer_packs(s);
    return;
  }
  if (!result) {
    if (current) {
      f.state = ThemeFetch::State::Failed;
      f.reason = reason_of(result.error());
      // Settings may have closed since it was picked; then the row is not there to say so.
      if (!s.app.settings().is_open()) {
        std::string name(theme_name(s.app.ui().offered, skin, f.theme, locale::current().id));
        s.app.toasts().toast(locale::fill(tr().display_fetch_failed, {name.empty() ? f.theme : name, f.reason}));
      }
    }
    offer_packs(s);
    return;
  }
  const bool apply = current && f.apply;
  const std::string theme = f.theme;
  if (current) f = {};
  skins_installed(s);
  // Arrived but not readable, as when a parent changed: the current Skin stays, and the row says so.
  if (std::ranges::none_of(s.app.ui().skins, [&](const SkinInfo& info) { return info.id == skin; })) {
    if (current) {
      f = {.state = ThemeFetch::State::Failed, .skin = skin, .theme = theme, .apply = apply, .reason = tr().fetch_reason_unreadable};
      if (!s.app.settings().is_open()) s.app.toasts().toast(locale::fill(tr().display_fetch_failed, {theme, f.reason}));
    }
    return;
  }
  if (apply) s.app.settings().display().use_theme(skin, theme);
}

void wire(Shell& s) {
  follow_system_animations(s);
  wire_tabs(s);
  Shell* sp = &s;
  DisplayTab& display = s.app.settings().display();
  display.on_fetch = [sp](const std::string& skin, const std::string& theme) { fetch(*sp, skin, theme); };
  display.on_cancel_fetch = [sp] {
    ThemeFetch& f = sp->app.ui().theme_fetch;
    cancel_pack(*sp, f.skin);
    f = {};
  };
  log_connection(s);
  apply_theme(s);
  apply_chat(s);
  s.log.set_enabled(s.app.ui().prefs->get(pref::log_enabled));
  note_skipped(s, true);
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
  const bool fetching = suffix("-fetching");
  const bool failed = suffix("-failed");
  const bool tip = suffix("-tip");
  const bool to_end = suffix("-end");
  const bool on = suffix("-on");
  const bool big = suffix("-big");
  if (rest == "display") {
    open(SettingsPage::Display);
    if (on) prefs.set(pref::system_theme, true);
    if (big) prefs.set(pref::chat_scale, 150);
    if (tip) panel.display().click(!s.app.ui().skins.empty() ? DisplayTab::SkinThemeList : on ? DisplayTab::LightList : DisplayTab::ThemeList, 0, 0, false);
    if (fetching || failed) {
      // A pack a Repository offers, picked before it is installed.
      Ui& ui = s.app.ui();
      ui.offered = {{.id = "aurora", .names = {{"ko", "오로라"}, {"en", "Aurora"}}, .themes = {{.id = "dawn", .names = {{"ko", "새벽"}, {"en", "Dawn"}}}}, .to_fetch = 2'400'000}};
      ui.theme_fetch = {.state = fetching ? ThemeFetch::State::Fetching : ThemeFetch::State::Failed, .skin = "aurora", .theme = "dawn", .percent = 42,
                        .reason = failed ? tr().fetch_reason_network : ""};
    }
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
  if (key == "theme" || key == "systemTheme" || key == "lightTheme" || key == "darkTheme" || key == pref::skin.name || key == pref::skin_theme.name || key == pref::dark_mode.name) {
    apply_theme(s);
    repositories::show_packs(s);  // which one is in use
  } else if (key == "language") {
    apply_language(s);
    repositories::show_packs(s);  // their names
    s.app.room().chat().locale_changed();
    sound::language_changed(s);
    InvalidateRect(s.hwnd, nullptr, FALSE);
  } else if (key == "chatScale" || key == "use24h") {
    apply_chat(s);
    InvalidateRect(s.hwnd, nullptr, FALSE);
  } else if (key == pref::theme_cursor.name) {
    reapply_cursor(s);
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
  if (!s.prefs) return;
  const Prefs& prefs = *s.app.ui().prefs;
  if (s.app.ui().skins.empty() ? prefs.get(pref::system_theme) : choice_of(prefs, s.app.ui().skins).dark == DarkMode::System) apply_theme(s);
}

}  // namespace crpp::gui::area::preferences
