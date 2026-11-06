// Runs against the real packs in assets/sounds, whose names these rules must fit.
#include "crpp/sound/catalog.hpp"

#include <string>

#include "check.hpp"

using namespace crpp::sound;
using crpp::wire::Kind;

namespace {

std::string pick_name(const Catalog& catalog, Kind kind, std::string_view pack, Random& random) {
  const auto file = catalog.pick(kind, pack, random);
  return file ? file->filename().string() : std::string{};
}

void finds_the_packs_in_order() {
  const auto catalog = Catalog::scan(CRPP_SOUNDS_DIR);
  CHECK(!catalog.empty());
  CHECK((catalog.packs(Device::Keyboard) == std::vector<std::string>{"plastic", "thocky", "walnut"}));
  CHECK((catalog.packs(Device::Mouse) == std::vector<std::string>{"plastic", "rubber", "walnut"}));
  CHECK((catalog.packs(Device::Pen) == std::vector<std::string>{"paper"}));
}

void kinds_reach_their_devices() {
  CHECK(device_of(Kind::MouseLeft) == Device::Mouse);
  CHECK(device_of(Kind::MouseRightHold) == Device::Mouse);
  CHECK(device_of(Kind::KeyNormal) == Device::Keyboard);
  CHECK(device_of(Kind::KeyAlt) == Device::Keyboard);
  CHECK(device_of(Kind::KeyAny) == Device::Keyboard);
  CHECK(device_of(Kind::KeyR1) == Device::Keyboard);
  CHECK(device_of(Kind::PenTap) == Device::Pen);
}

void plain_sounds_match_by_name() {
  const auto catalog = Catalog::scan(CRPP_SOUNDS_DIR);
  Random random(1);
  CHECK(pick_name(catalog, Kind::KeySpace, "plastic", random) == "mod_space.wav");
  CHECK(pick_name(catalog, Kind::KeyEscape, "plastic", random) == "mod_esc.wav");
  CHECK(pick_name(catalog, Kind::MouseSideBack, "rubber", random) == "side_back.wav");
  CHECK(pick_name(catalog, Kind::PenTap, "paper", random).starts_with("pen_tap_"));
}

void variations_are_chosen_at_random() {
  const auto catalog = Catalog::scan(CRPP_SOUNDS_DIR);
  Random random(7);
  std::vector<std::string> seen;
  for (int i = 0; i < 200; ++i) {
    const std::string name = pick_name(catalog, Kind::KeyR1, "walnut", random);
    if (std::ranges::find(seen, name) == seen.end()) seen.push_back(name);
  }
  CHECK(seen.size() == 3);
}

void the_any_key_gets_some_letter_sound() {
  const auto catalog = Catalog::scan(CRPP_SOUNDS_DIR);
  Random random(3);
  std::vector<std::string> seen;
  for (int i = 0; i < 200; ++i) {
    const std::string name = pick_name(catalog, Kind::KeyAny, "plastic", random);
    CHECK(name.starts_with("alpha_r"));
    if (std::ranges::find(seen, name) == seen.end()) seen.push_back(name);
  }
  CHECK(seen.size() == 3);
}

void hold_and_release_come_in_pairs() {
  const auto catalog = Catalog::scan(CRPP_SOUNDS_DIR);
  Random random(1);
  CHECK(pick_name(catalog, Kind::KeyShiftHold, "plastic", random) == "mod_shift_hold.wav");
  CHECK(pick_name(catalog, Kind::KeyShiftUp, "plastic", random) == "mod_shift_release.wav");
  CHECK(pick_name(catalog, Kind::MouseLeftHold, "walnut", random) == "left_hold.wav");
  CHECK(pick_name(catalog, Kind::MouseLeftUp, "walnut", random) == "left_release.wav");
}

void missing_sounds_stay_silent() {
  const auto catalog = Catalog::scan(CRPP_SOUNDS_DIR);
  Random random(1);
  CHECK(pick_name(catalog, Kind::MouseWheel, "rubber", random).empty());  // this pack has no wheel
  CHECK(pick_name(catalog, Kind::KeyNormal, "plastic", random).empty());
  CHECK(pick_name(catalog, static_cast<Kind>(200), "plastic", random).empty());
}

void an_unknown_pack_means_the_default() {
  const auto catalog = Catalog::scan(CRPP_SOUNDS_DIR);
  Random random(1);
  CHECK(pick_name(catalog, Kind::KeySpace, "gone", random) == "mod_space.wav");
  CHECK(pick_name(catalog, Kind::KeySpace, "", random) == "mod_space.wav");
}

void a_missing_folder_is_empty() {
  const auto catalog = Catalog::scan(std::filesystem::path(CRPP_SOUNDS_DIR) / "no-such-folder");
  CHECK(catalog.empty());
  Random random(1);
  CHECK(!catalog.pick(Kind::KeySpace, "plastic", random));
}

void choices_have_the_names_in_pack_json() {
  const Catalog catalog = Catalog::scan(CRPP_SOUNDS_DIR);
  const auto keyboard = catalog.choices(Device::Keyboard);
  CHECK(keyboard.size() == 3);
  CHECK(keyboard[0].id == "plastic" && keyboard[0].name == "\xED\x94\x8C\xEB\x9D\xBC\xEC\x8A\xA4\xED\x8B\xB1");
  CHECK(keyboard[2].id == "walnut");
  const auto pen = catalog.choices(Device::Pen);
  CHECK(pen.size() == 1 && pen[0].id == "paper");
}

void notification_files_are_found_with_or_without_the_number() {
  const std::filesystem::path root(CRPP_SOUNDS_DIR);
  CHECK(notification_file(root, "cloud_chat")->filename() == "cloud_chat.wav");
  CHECK(notification_file(root, "01_cloud_chat")->filename() == "cloud_chat.wav");
  CHECK(notification_file(root, "02_cloud_enter")->filename() == "cloud_enter.wav");
  CHECK(!notification_file(root, "none"));
  CHECK(!notification_file(root / "none", "cloud_chat"));
}

}  // namespace

int main() {
  finds_the_packs_in_order();
  kinds_reach_their_devices();
  plain_sounds_match_by_name();
  variations_are_chosen_at_random();
  the_any_key_gets_some_letter_sound();
  hold_and_release_come_in_pairs();
  missing_sounds_stay_silent();
  an_unknown_pack_means_the_default();
  a_missing_folder_is_empty();
  choices_have_the_names_in_pack_json();
  notification_files_are_found_with_or_without_the_number();
  return crpp_test_result();
}
