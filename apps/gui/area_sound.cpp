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
  crpp::sound::IdleWatch idle;
  bool in_room = false;
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

// --- the tab's lists ---

// False if there were none, as before the assets are in place.
bool list_packs(Shell& s) {
  using crpp::sound::Device;
  const auto catalog = crpp::sound::Catalog::scan(s.assets_dir / "sounds", s.expected_sounds, locale::current().id);
  SoundTab& tab = s.app.settings().sound();
  const auto set = [&](SoundDevice to, Device from) {
    std::vector<SoundChoice> choices;
    for (auto& pack : catalog.choices(from)) choices.push_back({std::move(pack.id), std::move(pack.name)});
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

void tab_shown(Shell& s) {
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

void wire(Shell& s) {
  Shell* sp = &s;
  s.sound_state = std::make_shared<State>();
  timer_shell = &s;
  State& state = *s.sound_state;
  state.volumes.load(s.store->find("userVolumes"));

  // main.cpp read most of these for the first sound; here comes the rest.
  apply(s);
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
  if (is_pack_key(key)) tell_server_my_sounds(s);
  save_soon(s);
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
