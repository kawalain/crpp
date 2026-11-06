#include "../sound_prefs.hpp"

#include "check.hpp"

using namespace crpp;
using namespace crpp::gui;

namespace {

void settings_come_from_the_prefs() {
  ui::MemoryPrefs prefs;
  sound::Settings audio;
  audio.muted = true;
  audio.idle_muted = true;
  read_sound_settings(prefs, audio);
  CHECK(audio.volume == 30);
  CHECK(audio.keyboard_volume == 100);
  CHECK(audio.keyboard_pack == "plastic");
  CHECK(audio.pen_pack == "paper");
  CHECK(audio.balance == 60);
  CHECK(audio.spatial);
  CHECK(audio.device.empty());
  CHECK(audio.muted && audio.idle_muted);  // not settings

  prefs.set(ui::pref::keyboard_sound, "walnut");
  prefs.set(ui::pref::listen_self, true);
  prefs.set(ui::pref::others_use_my_sounds, true);
  prefs.set(ui::pref::mute_notifications_on_mute, true);
  prefs.set(ui::pref::spatial_audio, false);
  prefs.set(ui::pref::speaker_device, "id-a");
  prefs.set(ui::pref::channel_balance, 80);
  prefs.set(ui::pref::volume, 400);
  read_sound_settings(prefs, audio);
  CHECK(audio.keyboard_pack == "walnut");
  CHECK(audio.listen_self && audio.others_use_my_sounds && audio.mute_notifications);
  CHECK(!audio.spatial);
  CHECK(audio.device == "id-a");
  CHECK(audio.balance == 80);
  CHECK(audio.volume == 100);

  prefs.set(ui::pref::channel_balance_enabled, false);
  read_sound_settings(prefs, audio);
  CHECK(audio.balance == 0);
  // A value from elsewhere stays on the slider's scale.
  prefs.set(ui::pref::channel_balance_enabled, true);
  prefs.set(ui::pref::channel_balance, 10);
  read_sound_settings(prefs, audio);
  CHECK(audio.balance == 55);
}

void only_the_keys_of_this_area_count() {
  CHECK(is_sound_key("volume"));
  CHECK(is_sound_key("muteOthers"));
  CHECK(is_sound_key("channelBalance"));
  CHECK(is_sound_key("speakerDevice"));
  CHECK(!is_sound_key("theme"));
  CHECK(!is_sound_key("speakerDeviceName"));  // set just before the device itself
  CHECK(is_pack_key("mouseSound"));
  CHECK(!is_pack_key("mouseVolume"));
}

void the_emoji_others_see_shows_being_away() {
  CHECK(presence_emoji("1f600", false) == "1f600");
  CHECK(presence_emoji("1f600", true) == "1f634");
  CHECK(presence_emoji("", false) == "1f4a7");
}

void a_member_with_an_account_is_saved_by_uid() {
  MemberVolumes volumes;
  volumes.identify("cid-a", "uid-a");
  CHECK(volumes.get("cid-a") == 100);
  CHECK(volumes.set("cid-a", 40));
  CHECK(volumes.get("cid-a") == 40);
  CHECK(volumes.saved().dump() == R"({"uid-a":40})");
  // 100 is the default and is not kept.
  CHECK(volumes.set("cid-a", 100));
  CHECK(volumes.saved().dump() == "{}");
}

void a_guest_lasts_as_long_as_the_room() {
  MemberVolumes volumes;
  CHECK(!volumes.set("cid-g", 0));
  CHECK(volumes.get("cid-g") == 0);
  CHECK(volumes.saved().dump() == "{}");
  volumes.left_room();
  CHECK(volumes.get("cid-g") == 100);
}

void saved_volumes_return_in_a_later_room() {
  const auto saved = store::Json::parse(R"({"uid-a":0,"uid-b":250,"uid-c":"x"})");
  CHECK(saved.has_value());
  MemberVolumes volumes;
  volumes.load(&*saved);
  volumes.identify("other-cid", "uid-a");
  CHECK(volumes.get("other-cid") == 0);
  volumes.identify("cid-b", "uid-b");
  CHECK(volumes.get("cid-b") == 100);
  volumes.left_room();
  CHECK(volumes.get("other-cid") == 100);  // not known until identified again
  volumes.identify("other-cid", "uid-a");
  CHECK(volumes.get("other-cid") == 0);
  volumes.load(nullptr);
  CHECK(volumes.saved().dump() == "{}");
}

void a_volume_set_before_the_uid_arrived_goes_with_it() {
  MemberVolumes volumes;
  (void)volumes.set("cid-a", 30);
  volumes.identify("cid-a", "uid-a");
  CHECK(volumes.get("cid-a") == 30);
  CHECK(volumes.saved().dump() == R"({"uid-a":30})");
}

}  // namespace

int main() {
  settings_come_from_the_prefs();
  only_the_keys_of_this_area_count();
  the_emoji_others_see_shows_being_away();
  a_member_with_an_account_is_saved_by_uid();
  a_guest_lasts_as_long_as_the_room();
  saved_volumes_return_in_a_later_room();
  a_volume_set_before_the_uid_arrived_goes_with_it();
  return crpp_test_result();
}
