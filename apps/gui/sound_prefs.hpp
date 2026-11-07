#pragma once
// From the settings as saved to what the sound engine runs on, and the members' saved
// volumes. Free of the window, so the tests can run it.
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include "crpp/sound/engine.hpp"
#include "crpp/store/json.hpp"
#include "crpp/ui/prefs.hpp"

namespace crpp::gui {

// A name this version does not know is the default.
[[nodiscard]] inline sound::Reverb reverb_named(std::string_view name) {
  if (name == "off") return sound::Reverb::Off;
  return name == "large" ? sound::Reverb::Large : sound::Reverb::Office;
}

// Leaves what comes from elsewhere alone: `muted` and `idle_muted`.
inline void read_sound_settings(const ui::Prefs& prefs, sound::Settings& out) {
  const auto percent = [&](ui::pref::Int key) { return std::clamp(prefs.get(key), 0, 100); };
  out.volume = percent(ui::pref::volume);
  out.keyboard_volume = percent(ui::pref::keyboard_volume);
  out.mouse_volume = percent(ui::pref::mouse_volume);
  out.pen_volume = percent(ui::pref::pen_volume);
  out.keyboard_pack = prefs.get(ui::pref::keyboard_sound);
  out.mouse_pack = prefs.get(ui::pref::mouse_sound);
  out.pen_pack = prefs.get(ui::pref::pen_sound);
  out.listen_self = prefs.get(ui::pref::listen_self);
  out.others_use_my_sounds = prefs.get(ui::pref::others_use_my_sounds);
  out.mute_notifications = prefs.get(ui::pref::mute_notifications_on_mute);
  out.spatial = prefs.get(ui::pref::spatial_audio);
  out.balance = prefs.get(ui::pref::channel_balance_enabled) ? std::clamp(prefs.get(ui::pref::channel_balance), 55, 90) : 0;
  out.reverb = reverb_named(prefs.get(ui::pref::reverb));
  out.device = prefs.get(ui::pref::speaker_device);
}

// The settings this area reacts to.
[[nodiscard]] inline bool is_sound_key(std::string_view key) {
  namespace p = ui::pref;
  for (const char* name : {p::mute_others.name, p::volume.name, p::keyboard_volume.name, p::mouse_volume.name, p::pen_volume.name,
                           p::speaker_device.name, p::keyboard_sound.name, p::mouse_sound.name, p::pen_sound.name,
                           p::listen_self.name, p::others_use_my_sounds.name, p::mute_notifications_on_mute.name,
                           p::spatial_audio.name, p::channel_balance_enabled.name, p::channel_balance.name, p::reverb.name}) {
    if (key == name) return true;
  }
  return false;
}

// The emoji others see: SLEEP_EMOJI while away, without touching the saved one. Never
// empty, as sending none would leave the sleeping face up after coming back.
[[nodiscard]] inline std::string presence_emoji(std::string saved, bool away) {
  if (away) return "1f634";
  return saved.empty() ? "1f4a7" : saved;
}

[[nodiscard]] inline bool is_pack_key(std::string_view key) {
  return key == ui::pref::keyboard_sound.name || key == ui::pref::mouse_sound.name || key == ui::pref::pen_sound.name;
}

// What the user set for each member, 0 being muted and 100, the default, not being
// kept. As upstream: a member with an account is remembered by uid in "userVolumes"
// for good, a guest by cid until the Room is left.
class MemberVolumes {
 public:
  void load(const store::Json* saved) {
    by_uid_.clear();
    const auto* object = saved ? saved->as_object() : nullptr;
    if (!object) return;
    for (const auto& member : *object) {
      if (const auto percent = member.value.as_i64()) {
        by_uid_[member.key] = static_cast<int>(std::clamp<std::int64_t>(*percent, 0, 100));
      }
    }
  }

  [[nodiscard]] store::Json saved() const {
    store::Json object = store::Json::object();
    for (const auto& [uid, percent] : by_uid_) object.set(uid, store::Json(percent));
    return object;
  }

  // The server said who this is. A volume set before that goes with them.
  void identify(std::string_view cid, std::string_view uid) {
    if (uid.empty()) return;
    const std::string key(cid);
    uid_of_[key] = std::string(uid);
    if (const auto session = by_cid_.find(key); session != by_cid_.end()) {
      by_uid_[std::string(uid)] = session->second;
      by_cid_.erase(session);
    }
  }

  [[nodiscard]] int get(std::string_view cid) const {
    const std::string key(cid);
    if (const auto uid = uid_of_.find(key); uid != uid_of_.end()) {
      const auto found = by_uid_.find(uid->second);
      return found == by_uid_.end() ? 100 : found->second;
    }
    const auto found = by_cid_.find(key);
    return found == by_cid_.end() ? 100 : found->second;
  }

  // True if "userVolumes" must be saved again.
  bool set(std::string_view cid, int percent) {
    percent = std::clamp(percent, 0, 100);
    const std::string key(cid);
    const auto uid = uid_of_.find(key);
    auto& values = uid == uid_of_.end() ? by_cid_ : by_uid_;
    const std::string& where = uid == uid_of_.end() ? key : uid->second;
    if (percent == 100) values.erase(where);
    else values[where] = percent;
    return uid != uid_of_.end();
  }

  void left_room() {
    by_cid_.clear();
    uid_of_.clear();
  }

 private:
  std::map<std::string, int> by_uid_, by_cid_;
  std::map<std::string, std::string> uid_of_;
};

}  // namespace crpp::gui
