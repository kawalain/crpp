#include "crpp/input/classifier.hpp"

#include "check.hpp"

using namespace crpp::input;
using crpp::wire::Kind;
using crpp::wire::is_known;

namespace {

constexpr std::uint16_t key_a = 0x1E, key_q = 0x10, key_1 = 0x02, key_space = 0x39, key_enter = 0x1C;
constexpr std::uint16_t left_shift = 0x2A, right_shift = 0x36, ctrl = 0x1D, alt = 0x38;

void presence_says_nothing_about_the_key() {
  // Every possible key, also the special ones, gives one and the same kind.
  for (std::uint16_t make = 0; make < 256; ++make) {
    for (const bool extended : {false, true}) {
      KeyClassifier c;
      CHECK(c.on_key(make, extended, false) == Kind::KeyAny);
      CHECK(!c.on_key(make, extended, true));
    }
  }
}

void detail_names_special_keys_and_rows() {
  KeyClassifier c;
  c.set_privacy(Privacy::Detail);
  const auto press = [&](std::uint16_t make, bool extended = false) {
    const auto kind = c.on_key(make, extended, false);
    (void)c.on_key(make, extended, true);
    return kind;
  };
  CHECK(press(key_space) == Kind::KeySpace);
  CHECK(press(key_enter) == Kind::KeyEnter);
  CHECK(press(key_enter, true) == Kind::KeyEnter);  // the keypad's
  CHECK(press(0x01) == Kind::KeyEscape);
  CHECK(press(0x0E) == Kind::KeyBackspace);
  CHECK(press(0x0F) == Kind::KeyTab);
  CHECK(press(0x3A) == Kind::KeyCaps);
  CHECK(press(left_shift) == Kind::KeyShiftHold);
  CHECK(press(right_shift) == Kind::KeyShiftHold);
  CHECK(press(ctrl, true) == Kind::KeyCtrlHold);
  CHECK(press(alt, true) == Kind::KeyAltHold);
  // Letters and digits say their row, by the key's place.
  CHECK(press(0x2C) == Kind::KeyR1);  // Z
  CHECK(press(0x35) == Kind::KeyR1);  // /
  CHECK(press(0x56) == Kind::KeyR1);  // the ISO key beside Z
  CHECK(press(key_a) == Kind::KeyR2);
  CHECK(press(0x28) == Kind::KeyR2);  // '
  CHECK(press(key_q) == Kind::KeyR3);
  CHECK(press(0x1B) == Kind::KeyR3);  // ]
  CHECK(press(0x2B) == Kind::KeyR3);  // backslash
  CHECK(press(key_1) == Kind::KeyR4);
  CHECK(press(0x0D) == Kind::KeyR4);  // =
  CHECK(press(0x29) == Kind::KeyR4);  // `
  // Off the four rows nothing is said. E0 marks another key with the same code.
  CHECK(press(0x3B) == Kind::KeyAny);        // F1
  CHECK(press(0x48, true) == Kind::KeyAny);  // up
  CHECK(press(0x35, true) == Kind::KeyAny);  // the keypad's /
  CHECK(press(0x4F) == Kind::KeyAny);        // the keypad's 1
}

void rows_make_no_sound_on_release() {
  KeyClassifier c;
  c.set_privacy(Privacy::Detail);
  CHECK(c.on_key(key_a, false, false) == Kind::KeyR2);
  CHECK(!c.on_key(key_a, false, false));  // auto-repeat
  CHECK(!c.on_key(key_a, false, true));
}

void only_modifiers_sound_on_release() {
  KeyClassifier c;
  c.set_privacy(Privacy::Detail);
  (void)c.on_key(left_shift, false, false);
  CHECK(c.on_key(left_shift, false, true) == Kind::KeyShiftUp);
  (void)c.on_key(ctrl, false, false);
  CHECK(c.on_key(ctrl, false, true) == Kind::KeyCtrlUp);
  (void)c.on_key(alt, false, false);
  CHECK(c.on_key(alt, false, true) == Kind::KeyAltUp);
  (void)c.on_key(key_space, false, false);
  CHECK(!c.on_key(key_space, false, true));
  CHECK(!c.on_key(left_shift, false, true));  // released without a press seen
}

void auto_repeat_is_silent() {
  KeyClassifier c;
  CHECK(c.on_key(key_a, false, false).has_value());
  CHECK(!c.on_key(key_a, false, false));
  CHECK(!c.on_key(key_a, false, false));
  CHECK(c.on_key(key_q, false, false).has_value());  // another key meanwhile
  CHECK(!c.on_key(key_a, false, true));
  CHECK(c.on_key(key_a, false, false).has_value());
}

void reset_forgets_held_keys() {
  KeyClassifier c;
  (void)c.on_key(key_a, false, false);
  c.reset();
  CHECK(c.on_key(key_a, false, false).has_value());
}

void extended_keys_are_tracked_apart() {
  KeyClassifier c;
  CHECK(c.on_key(ctrl, false, false).has_value());
  CHECK(c.on_key(ctrl, true, false).has_value());
}

void out_of_range_codes_are_safe() {
  KeyClassifier c;
  CHECK(c.on_key(0xFFFF, true, false).has_value());
  CHECK(!c.on_key(0xFFFF, true, false));
}

void every_kind_is_accepted_by_the_server() {
  for (const auto action : {MouseAction::LeftDown, MouseAction::LeftUp, MouseAction::MiddleDown, MouseAction::MiddleUp,
                            MouseAction::RightDown, MouseAction::RightUp, MouseAction::BackDown, MouseAction::ForwardDown,
                            MouseAction::Wheel}) {
    CHECK(is_known(mouse_kind(action)));
  }
  CHECK(is_known(Kind::KeyAny));
  CHECK(mouse_kind(MouseAction::LeftDown) == Kind::MouseLeftHold);
  CHECK(mouse_kind(MouseAction::Wheel) == Kind::MouseWheel);
}

}  // namespace

int main() {
  presence_says_nothing_about_the_key();
  detail_names_special_keys_and_rows();
  rows_make_no_sound_on_release();
  only_modifiers_sound_on_release();
  auto_repeat_is_silent();
  reset_forgets_held_keys();
  extended_keys_are_tracked_apart();
  out_of_range_codes_are_safe();
  every_kind_is_accepted_by_the_server();
  return crpp_test_result();
}
