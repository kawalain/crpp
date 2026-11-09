#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "areas.hpp"
#include "crpp/sound/catalog.hpp"
#include "crpp/sound/idle.hpp"
#include "shell.hpp"
#include "sound_prefs.hpp"

using namespace crpp::ui;

namespace crpp::gui::area::sound {

struct State {
  MemberVolumes volumes;
  // What each device last had that was in place: where a pack that cannot be fetched goes back to.
  std::array<std::string, 3> settled;
  crpp::sound::IdleWatch idle;
  bool in_room = false;
  bool hotkey_held = false;  // the mute shortcut, registered while in a Room
  bool packs_listed = false;
  std::atomic<bool> devices_posted{false};
  std::unique_ptr<crpp::sound::OutputDevices> devices;  // from the first look at the tab
};

namespace {

constexpr UINT_PTR idle_timer = 101;  // WM_TIMER goes to its own procedure, not the window's
Shell* timer_shell = nullptr;

// Writes reach the disk with the shell's delayed save, which a UI task starts.
void save_soon(Shell& s) {
  s.tasks.post([] {});
}

void apply(Shell& s) {
  read_sound_settings(*s.prefs, s.audio);
  s.audio.muted = s.prefs->get(pref::mute_others);
  s.audio.idle_muted = s.sound_state->idle.idle() && s.prefs->get(pref::mute_on_idle);
  s.audio.chimes = s.app.notification_style() == 2 ? crpp::sound::ChimeStyle::Plain : s.app.notification_style() == 1 ? crpp::sound::ChimeStyle::Bells : crpp::sound::ChimeStyle::None;
  s.engine->set_settings(s.audio);
}

void tell_server_my_sounds(Shell& s) {
  if (!s.client) return;
  const std::string emoji = presence_emoji(s.store->string_or("emoji", ""), s.sound_state->idle.idle());
  session::event::SetSounds sounds;
  sounds.key = s.audio.keyboard_pack;
  sounds.mouse = s.audio.mouse_pack;
  sounds.pen = s.audio.pen_pack;
  if (!emoji.empty()) sounds.emoji = emoji;
  s.client->set_sounds(sounds);
}

// --- being away ---

std::uint32_t idle_limit_ms(const Shell& s) {
  const int minutes = s.prefs->get(pref::idle_minutes);
  return static_cast<std::uint32_t>(minutes > 0 ? minutes : pref::idle_minutes.fallback) * 60000;
}

void sync_idle(Shell& s);

void CALLBACK idle_timer_fired(HWND, UINT, UINT_PTR, DWORD) {
  if (timer_shell) sync_idle(*timer_shell);
}

void arm_idle(Shell& s, const crpp::sound::IdleWatch::Step& step) {
  // One wake-up, for the moment the limit can first be reached; none while away.
  if (step.check_after_ms > 0) SetTimer(s.hwnd, idle_timer, step.check_after_ms, idle_timer_fired);
  else KillTimer(s.hwnd, idle_timer);
}

void idle_changed(Shell& s, bool was_idle) {
  if (s.sound_state->idle.idle() == was_idle) return;
  apply(s);
  tell_server_my_sounds(s);
}

// Only in a Room: otherwise there is no timer at all. The mute also depends on the
// setting, the away emoji does not.
void sync_idle(Shell& s) {
  State& state = *s.sound_state;
  const bool was_idle = state.idle.idle();
  if (!state.in_room) {
    KillTimer(s.hwnd, idle_timer);
    state.idle.reset();
  } else {
    LASTINPUTINFO last{sizeof last, 0};
    const DWORD now = GetTickCount();
    if (!GetLastInputInfo(&last)) last.dwTime = now;
    arm_idle(s, state.idle.check(now, last.dwTime, idle_limit_ms(s)));
  }
  idle_changed(s, was_idle);
}

// --- the packs of the Repositories the user added ---

constexpr std::array<const ui::pref::Text*, 3> pack_keys{&pref::keyboard_sound, &pref::mouse_sound, &pref::pen_sound};
constexpr std::array<const char*, 3> kind_names{"keyboard", "mouse", "pen"};

// Installed, or on its way, whose files the engine may expect.
bool in_use_here(const Shell& s, const AddedRepository& added, const std::vector<std::string>& installed, std::string_view id) {
  return std::ranges::find(installed, assets::sound_group(id)) != installed.end() || sound_coming(s, added.id + "/" + std::string(id));
}

std::vector<crpp::sound::PackRoot> added_roots(const Shell& s) {
  std::vector<crpp::sound::PackRoot> roots;
  for (const AddedRepository& added : s.added) {
    crpp::sound::PackRoot root{added_files(s, added).under("sounds/"), added.id};
    if (added.catalog) {
      const auto installed = assets::kept_groups(*added.catalog, added.dir);
      for (const assets::File& file : added.catalog->files) {
        if (!file.group.starts_with("sound:") || !file.path.starts_with("sounds/") || root.tree.find(file.path.substr(7))) continue;
        if (in_use_here(s, added, installed, std::string_view(file.group).substr(6))) root.tree.add(file.path.substr(7), s.files_dir / file.sha256);
      }
    }
    roots.push_back(std::move(root));
  }
  return roots;
}

// --- the tab's lists ---

// False if there were none, as before the assets are in place.
bool list_packs(Shell& s) {
  using crpp::sound::Device;
  std::vector<crpp::sound::PackRoot> roots{{official_sounds(s), {}}};
  for (auto& root : added_roots(s)) roots.push_back(std::move(root));
  const auto catalog = crpp::sound::Catalog::scan(roots, locale::current().id);
  // Then those of the added Repositories not installed, with what fetching them takes.
  const std::string_view locale = locale::current().id;
  std::array<std::vector<SoundChoice>, 3> offered;
  for (const AddedRepository& added : s.added) {
    if (!added.catalog) continue;
    const auto installed = assets::kept_groups(*added.catalog, added.dir);
    for (const assets::SoundPack& pack : added.catalog->sounds) {
      if (in_use_here(s, added, installed, pack.id)) continue;
      std::uint64_t bytes = 0;
      for (const assets::File& file : added.catalog->files) {
        if (file.group == assets::sound_group(pack.id)) bytes += file.size;
      }
      const std::string label = locale::fill(tr().display_theme_fetch, {std::string(name_in(pack.names, locale, pack.id)), megabytes(bytes)});
      for (const std::string& kind : assets::sound_kinds(*added.catalog, pack.id)) {
        const auto at = std::ranges::find(kind_names, kind);
        if (at != kind_names.end()) offered[static_cast<std::size_t>(at - kind_names.begin())].push_back({added.id + "/" + pack.id, label});
      }
    }
  }
  SoundTab& tab = s.app.settings().sound();
  const auto set = [&](SoundDevice to, Device from) {
    std::vector<SoundChoice> choices;
    for (auto& pack : catalog.choices(from)) choices.push_back({std::move(pack.id), std::move(pack.name)});
    for (SoundChoice& more : offered[static_cast<std::size_t>(to)]) choices.push_back(std::move(more));
    tab.set_packs(to, std::move(choices));
  };
  set(SoundDevice::Keyboard, Device::Keyboard);
  set(SoundDevice::Mouse, Device::Mouse);
  set(SoundDevice::Pen, Device::Pen);
  return !catalog.empty();
}

void list_speakers(Shell& s) {
  std::vector<SoundChoice> choices;
  for (auto& device : s.sound_state->devices->list()) choices.push_back({std::move(device.id), std::move(device.name)});
  s.app.settings().sound().set_speakers(std::move(choices));
}

// --- the mute shortcut ---

constexpr int mute_hotkey_id = 1;  // RegisterHotKey's id, of the main window

// The shortcut is held only in a Room, where it acts, so that out of one it is left to other programs. Out of a Room
// a choice is only tried and let go, to say whether another program holds it. True if another program holds it.
bool sync_hotkey(Shell& s, bool try_now) {
  if (!s.hwnd) return false;  // a shot, which has no window and leaves the system's shortcuts alone
  State& st = *s.sound_state;
  if (st.hotkey_held) {
    UnregisterHotKey(s.hwnd, mute_hotkey_id);
    st.hotkey_held = false;
  }
  SoundTab& tab = s.app.settings().sound();
  const MuteHotkey* key = mute_hotkey_named(s.prefs->get(pref::mute_hotkey));
  bool taken = false;
  if (!key) {
    tab.set_hotkey_taken(false);
  } else if (st.in_room || try_now) {
    // Without MOD_NOREPEAT a held key would toggle again and again.
    const bool held = RegisterHotKey(s.hwnd, mute_hotkey_id, key->modifiers | MOD_NOREPEAT, key->vk) != 0;
    taken = !held;
    tab.set_hotkey_taken(taken);
    if (held && st.in_room) st.hotkey_held = true;
    else if (held) UnregisterHotKey(s.hwnd, mute_hotkey_id);
  }
  InvalidateRect(s.hwnd, nullptr, FALSE);
  invalidate_panes(s);
  return taken;
}

void tab_shown(Shell& s) {
  // Another program may have taken the shortcut since; in a Room it is held already.
  if (!s.sound_state->in_room) sync_hotkey(s, true);
  State& state = *s.sound_state;
  if (!state.packs_listed) state.packs_listed = list_packs(s);
  if (!state.devices) {
    state.devices = std::make_unique<crpp::sound::OutputDevices>();
    Shell* sp = &s;
    // On Windows' thread, where nothing may wait: one task at a time, and the queue's
    // lock is only ever held to swap it.
    state.devices->watch([sp] {
      if (sp->sound_state->devices_posted.exchange(true)) return;
      sp->tasks.post([sp] {
        sp->sound_state->devices_posted = false;
        // The tab lists them again whenever it comes back on screen.
        auto& panel = sp->app.settings();
        if (panel.is_open() && panel.page() == SettingsPage::Sound) list_speakers(*sp);
      });
    });
  }
  list_speakers(s);
}

}  // namespace

void language_changed(Shell& s) { s.sound_state->packs_listed = false; }

void packs_changed(Shell& s) {
  s.engine->set_added(added_roots(s));
  if (!s.sound_state) return;
  s.sound_state->packs_listed = false;
  auto& panel = s.app.settings();
  if (panel.is_open() && panel.page() == SettingsPage::Sound) s.sound_state->packs_listed = list_packs(s);
}

std::string pack_name(const Shell& s, std::string_view id) {
  const auto at = repositories::place(s, id);
  if (!at || at->first == 0) return std::string(id);
  const AddedRepository& added = s.added[at->first - 1];
  const assets::SoundPack* pack = added.catalog ? added.catalog->sound(at->second) : nullptr;
  return pack ? std::string(name_in(pack->names, locale::current().id, pack->id)) : std::string(at->second);
}

void pack_fetched(Shell& s, const std::string& id, const std::expected<void, assets::Error>& result) {
  for (std::size_t i = 0; i < pack_keys.size(); ++i) {
    if (s.prefs->get(*pack_keys[i]) != id) continue;
    if (result) {
      s.sound_state->settled[i] = id;
    } else {
      // Back to what was in place, which sets the key again.
      s.prefs->set(*pack_keys[i], s.sound_state->settled[i]);
    }
  }
  if (!result) {
    s.log.note(DiagEvent::SoundNotFetched, id);
    s.app.toasts().toast(locale::fill(tr().sound_fetch_failed, {pack_name(s, id)}));
  }
  packs_changed(s);
  repositories::show_packs(s);
}

void wire(Shell& s) {
  Shell* sp = &s;
  s.sound_state = std::make_shared<State>();
  timer_shell = &s;
  State& state = *s.sound_state;
  state.volumes.load(s.store->find("userVolumes"));

  // main.cpp read most of these for the first sound; here comes the rest.
  apply(s);
  for (std::size_t i = 0; i < pack_keys.size(); ++i) state.settled[i] = s.prefs->get(*pack_keys[i]);
  s.engine->set_added(added_roots(s));
  s.app.room().set_volume(s.audio.volume);

  s.app.room().on_mute = [sp](bool muted) {
    sp->engine->play_system(muted ? "mute" : "unmute");
  };
  s.app.room().on_volume = [sp](int percent) { sp->prefs->set(pref::volume, percent); };

  SoundTab& tab = s.app.settings().sound();
  tab.on_shown = [sp] { tab_shown(*sp); };
  tab.on_speaker_picked = [sp] { sp->engine->check_device(); };
  s.engine->set_on_open_failed([sp] {
    sp->tasks.post([sp] { sp->app.toasts().toast(tr().sound_output_failed); });
  });

  s.bridge->on_member_ids = [sp](const std::vector<MemberIds>& members) {
    MemberVolumes& volumes = sp->sound_state->volumes;
    for (const MemberIds& member : members) {
      volumes.identify(member.cid, member.uid);
      const int percent = volumes.get(member.cid);
      if (percent != 100) sp->engine->set_member_volume(member.cid, percent);
    }
  };
  auto previous = std::move(s.bridge->on_room_changed);
  s.bridge->on_room_changed = [sp, previous = std::move(previous)](bool in_room) {
    if (previous) previous(in_room);
    State& st = *sp->sound_state;
    if (in_room == st.in_room) return;
    st.in_room = in_room;
    if (!in_room) {
      // A guest's volume lasts as long as the Room.
      st.volumes.left_room();
      sp->engine->clear_member_volumes();
    }
    sync_idle(*sp);
    // Taken since it was chosen: the settings row says so, but nobody looks there on entering a Room.
    if (sync_hotkey(*sp, false) && in_room) sp->app.toasts().toast(tr().mute_hotkey_busy);
  };
}

bool scene(Shell& s, std::string_view name) {
  if (!name.starts_with("settings-sound")) return false;
  s.app.settings().set_version("v0.0.0");
  (void)list_packs(s);
  SoundTab& tab = s.app.settings().sound();
  tab.set_speakers({{"a", "스피커 (Realtek High Definition Audio)"}, {"b", "헤드셋 (USB Audio Device)"}});
  s.app.settings().open(SettingsPage::Sound);
  if (name == "settings-sound-dark" || name == "settings-sound-effects-dark") s.app.set_theme("darkcloud");
  if (name == "settings-sound-effects-flat") s.app.ui().prefs->set(pref::spatial_audio, false);
  if (name == "settings-sound-hotkey-taken") {  // another program holds the shortcut
    s.app.ui().prefs->set(pref::mute_hotkey, "ctrl_alt_m");
    tab.set_hotkey_taken(true);
    s.app.settings().scroll_to(10000);
  }
  if (name == "settings-sound-effects" || name == "settings-sound-effects-dark" || name == "settings-sound-effects-flat") {
    s.app.settings().scroll_to(10000);
  }
  if (name == "settings-sound-open") {
    tab.click(SoundTab::SpeakerList, 0, 0, false);
    s.app.ui().hover_owner = &tab;
    s.app.ui().hover_id = SoundTab::KeyboardVolume;
  }
  return true;
}

void pref_changed(Shell& s, std::string_view key) {
  if (!s.sound_state) return;
  if (key == pref::mute_hotkey.name) {
    sync_hotkey(s, true);
    save_soon(s);
    return;
  }
  if (key == pref::mute_on_idle.name || key == pref::idle_minutes.name) {
    sync_idle(s);
    // Whether being away mutes may have changed though being away did not.
    apply(s);
    save_soon(s);
    return;
  }
  if (!is_sound_key(key)) return;
  apply(s);
  if (key == pref::volume.name) s.app.room().set_volume(s.audio.volume);
  if (is_pack_key(key)) {
    // One of an added Repository not installed comes now; meanwhile stand-ins play for it. One in
    // place is where a failure goes back to.
    for (std::size_t i = 0; i < pack_keys.size(); ++i) {
      if (key != pack_keys[i]->name) continue;
      const std::string id = s.prefs->get(*pack_keys[i]);
      const auto at = repositories::place(s, id);
      const AddedRepository* added = at && at->first > 0 ? &s.added[at->first - 1] : nullptr;
      const std::string group[] = {assets::sound_group(at ? at->second : std::string())};
      const bool missing = added && added->catalog && added->catalog->sound(at->second) && !assets::missing(*added->catalog, group, added->dir, s.files_dir).empty();
      if (missing && fetch_sound(s, id)) packs_changed(s);
      else if (!missing) s.sound_state->settled[i] = id;
    }
    tell_server_my_sounds(s);
  }
  save_soon(s);
}

void hotkey_pressed(Shell& s) {
  if (!s.sound_state || !s.sound_state->in_room || s.app.view() != View::Session) return;
  // As the Room's button, which says so and does the rest, also with the window hidden.
  s.app.room().click(RoomScreen::Mute, 0, 0, false);
}

void own_input(Shell& s) {
  if (!s.sound_state) return;
  State& state = *s.sound_state;
  if (!state.idle.idle()) return;  // the timer looks at the OS's last input when due
  arm_idle(s, state.idle.input(GetTickCount(), idle_limit_ms(s)));
  idle_changed(s, true);
}

void set_member_volume(Shell& s, std::string_view cid, int percent) {
  if (!s.sound_state) return;
  if (s.sound_state->volumes.set(cid, percent)) {
    s.store->set("userVolumes", s.sound_state->volumes.saved());
    save_soon(s);
  }
  s.engine->set_member_volume(std::string(cid), percent);
}

void set_member_muted(Shell& s, std::string_view cid, bool muted) {
  // Muted is a volume of 0, and unmuting goes back to 100, as upstream.
  set_member_volume(s, cid, muted ? 0 : 100);
}

int member_volume(Shell& s, std::string_view cid) { return s.sound_state ? s.sound_state->volumes.get(cid) : 100; }

bool member_muted(Shell& s, std::string_view cid) { return member_volume(s, cid) == 0; }

void theme_changed(Shell& s) {
  if (s.engine && s.sound_state) apply(s);
}

void play_notification(Shell& s, std::string_view name) {
  if (s.engine) s.engine->play_notification(name);
}

void preview_notification(Shell& s, std::string_view name) {
  if (s.engine) s.engine->preview_notification(name);
}

}  // namespace crpp::gui::area::sound
