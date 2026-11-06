#pragma once
// The settings, by upstream's key names and defaults (main.js defaultPreferences),
// so a Shared store stays readable by upstream. Keep each key's type as upstream's.
#include <array>
#include <map>
#include <string>
#include <string_view>

namespace crpp::ui {

namespace pref {

struct Bool {
  const char* name;
  bool fallback;
};
struct Int {
  const char* name;
  int fallback;
};
struct Text {
  const char* name;
  const char* fallback;
};

// --- sound tab ---
inline constexpr Int volume{"volume", 30};
inline constexpr Int keyboard_volume{"keyboardVolume", 100};
inline constexpr Int mouse_volume{"mouseVolume", 80};
inline constexpr Int pen_volume{"tabletVolume", 80};
inline constexpr Text speaker_device{"speakerDevice", ""};
inline constexpr Text speaker_device_name{"speakerDeviceName", ""};
inline constexpr Text keyboard_sound{"keyboardSound", "plastic"};
inline constexpr Text mouse_sound{"mouseSound", "plastic"};
inline constexpr Text pen_sound{"tabletSound", "paper"};
inline constexpr Bool listen_self{"listenSelf", false};
inline constexpr Bool others_use_my_sounds{"othersUseMySounds", false};
inline constexpr Bool mute_on_idle{"muteOnIdle", false};
inline constexpr Bool mute_notifications_on_mute{"muteNotificationsOnMute", false};
inline constexpr Bool spatial_audio{"spatialAudio", true};
inline constexpr Bool channel_balance_enabled{"channelBalanceEnabled", true};
inline constexpr Int channel_balance{"channelBalance", 60};

// --- display tab ---
inline constexpr Text theme{"theme", "cloudroom"};
inline constexpr Bool system_theme{"systemTheme", false};
inline constexpr Text light_theme{"lightTheme", "cloudroom"};
inline constexpr Text dark_theme{"darkTheme", "darkcloud"};
inline constexpr Int chat_scale{"chatScale", 100};

// --- notification tab ---
inline constexpr Bool chat_tray_notify{"chatTrayNotify", true};
inline constexpr Bool chat_taskbar_flash{"chatTaskbarFlash", true};

// --- system tab ---
inline constexpr Bool start_with_system{"startWithSystem", false};
inline constexpr Bool minimize_to_tray{"minimizeToTray", true};
inline constexpr Bool use_24h{"use24h", false};
inline constexpr Int idle_minutes{"idleMinutes", 30};
inline constexpr Bool log_enabled{"logEnabled", false};
// Not upstream's: see docs/adr/0003-presence-mode-by-default.md.
inline constexpr Bool input_detail{"inputDetail", false};

// --- pinned defaults of a new Room; not settings, so "설정 초기화" leaves them ---
inline constexpr Bool pin_public{"pinPublic", false};
inline constexpr Bool pin_chat{"pinChat", false};

// What "설정 초기화" puts back. Account and Room values are not settings.
inline constexpr std::array all_bool{listen_self,       others_use_my_sounds, mute_on_idle,    mute_notifications_on_mute,
                                     spatial_audio,     channel_balance_enabled, system_theme, chat_tray_notify,
                                     chat_taskbar_flash, start_with_system,   minimize_to_tray, use_24h,
                                     log_enabled,       input_detail};
inline constexpr std::array all_int{volume, keyboard_volume, mouse_volume, pen_volume, channel_balance, chat_scale, idle_minutes};
inline constexpr std::array all_text{speaker_device, speaker_device_name, keyboard_sound, mouse_sound, pen_sound,
                                     theme,          light_theme,         dark_theme};

}  // namespace pref

class Prefs {
 public:
  [[nodiscard]] virtual bool get(pref::Bool key) const = 0;
  [[nodiscard]] virtual int get(pref::Int key) const = 0;
  [[nodiscard]] virtual std::string get(pref::Text key) const = 0;
  virtual void set(pref::Bool key, bool value) = 0;
  virtual void set(pref::Int key, int value) = 0;
  virtual void set(pref::Text key, std::string_view value) = 0;

  void reset() {
    for (const auto key : pref::all_bool) set(key, key.fallback);
    for (const auto key : pref::all_int) set(key, key.fallback);
    for (const auto key : pref::all_text) set(key, key.fallback);
  }

 protected:
  ~Prefs() = default;
};

// For --shot and tests: starts at the defaults and forgets on exit.
class MemoryPrefs final : public Prefs {
 public:
  [[nodiscard]] bool get(pref::Bool key) const override {
    const auto it = bools_.find(key.name);
    return it == bools_.end() ? key.fallback : it->second;
  }
  [[nodiscard]] int get(pref::Int key) const override {
    const auto it = ints_.find(key.name);
    return it == ints_.end() ? key.fallback : it->second;
  }
  [[nodiscard]] std::string get(pref::Text key) const override {
    const auto it = texts_.find(key.name);
    return it == texts_.end() ? key.fallback : it->second;
  }
  void set(pref::Bool key, bool value) override { bools_[key.name] = value; }
  void set(pref::Int key, int value) override { ints_[key.name] = value; }
  void set(pref::Text key, std::string_view value) override { texts_[key.name] = std::string(value); }

 private:
  std::map<std::string, bool> bools_;
  std::map<std::string, int> ints_;
  std::map<std::string, std::string> texts_;
};

}  // namespace crpp::ui
