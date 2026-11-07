#include "crpp/sound/synth.hpp"

#include <algorithm>
#include <cmath>

#include "check.hpp"

using namespace crpp::sound;
using crpp::wire::Kind;

namespace {

constexpr std::uint32_t rate = 48000;

float peak_of(const std::vector<float>& s) {
  float peak = 0;
  for (const float v : s) peak = std::max(peak, std::abs(v));
  return peak;
}

// How much of the sound's energy is in sample-to-sample change: a measure of brightness.
float brightness(const std::vector<float>& s) {
  float change = 0, energy = 0;
  for (std::size_t i = 1; i < s.size(); ++i) {
    change += (s[i] - s[i - 1]) * (s[i] - s[i - 1]);
    energy += s[i] * s[i];
  }
  return energy > 0 ? change / energy : 0;
}

void every_kind_stays_short_finite_and_in_range() {
  for (const Kind kind : {Kind::MouseLeft, Kind::MouseLeftHold, Kind::MouseLeftUp, Kind::MouseWheel, Kind::KeyAny, Kind::KeyR1,
                          Kind::KeySpace, Kind::KeyShiftHold, Kind::KeyShiftUp, Kind::KeyEnter, Kind::PenTap, Kind::PenStroke}) {
    for (std::uint32_t variant = 0; variant < synth_variants; ++variant) {
      const auto s = synthesize(kind, "plastic", rate, variant);
      CHECK(!s.empty());
      CHECK(static_cast<float>(s.size()) <= synth_longest_ms / 1000 * rate);
      CHECK(peak_of(s) > 0.001f);
      CHECK(peak_of(s) <= 1.0f);
      CHECK(std::ranges::all_of(s, [](float v) { return std::isfinite(v); }));
      CHECK(std::abs(s.back()) < 0.01f);  // ends without a click
    }
  }
}

void the_same_request_gives_the_same_sound_and_variants_differ() {
  CHECK(synthesize(Kind::KeyAny, "walnut", rate, 1) == synthesize(Kind::KeyAny, "walnut", rate, 1));
  CHECK(synthesize(Kind::KeyAny, "walnut", rate, 0) != synthesize(Kind::KeyAny, "walnut", rate, 1));
}

void packs_keep_their_character() {
  // Rubber is far quieter and darker than plastic in the recordings.
  const auto plastic = synthesize(Kind::MouseLeft, "plastic", rate, 0);
  const auto rubber = synthesize(Kind::MouseLeft, "rubber", rate, 0);
  CHECK(peak_of(rubber) < peak_of(plastic) / 2);
  CHECK(brightness(rubber) < brightness(plastic));
  // An unknown pack is the device's default.
  CHECK(synthesize(Kind::MouseLeft, "gone", rate, 0) == plastic);
}

void a_release_is_lighter_than_a_press() {
  CHECK(peak_of(synthesize(Kind::KeyShiftUp, "plastic", rate, 0)) < peak_of(synthesize(Kind::KeyShift, "plastic", rate, 0)));
}

void cues_differ_and_fit_any_rate() {
  CHECK(synthesize_cue("mute", rate) != synthesize_cue("unmute", rate));
  CHECK(synthesize_cue("cloud_chat", rate) != synthesize_cue("cloud_enter", rate));
  CHECK(peak_of(synthesize_cue("cloud_chat", rate)) <= 1.0f);
  CHECK(synthesize_cue("cloud_chat", 96000).size() == 2 * synthesize_cue("cloud_chat", rate).size());
  CHECK(synthesize_cue("cloud_chat", 0).empty());
  CHECK(synthesize_cue("unknown", rate).empty());
  CHECK(!synthesize_cue("01_cloud_chat", rate).empty());
  CHECK(synthesize(Kind::KeyAny, "plastic", 0, 0).empty());
}

void chimes_are_short_bright_and_only_for_two_names() {
  for (const std::string_view name : {"cloud_chat", "01_cloud_chat", "cloud_enter"}) {
    const auto s = synthesize_chime(name, rate);
    CHECK(!s.empty());
    CHECK(static_cast<float>(s.size()) <= chime_longest_ms / 1000 * rate);
    CHECK(peak_of(s) > 0.3f && peak_of(s) <= 1.0f);
    CHECK(std::ranges::all_of(s, [](float v) { return std::isfinite(v); }));
    CHECK(std::abs(s.front()) < 0.01f && std::abs(s.back()) < 0.01f);  // without a click at either end
    CHECK(brightness(s) > brightness(synthesize_cue(name, rate)));
  }
  CHECK(synthesize_chime("cloud_chat", rate) != synthesize_chime("cloud_enter", rate));
  CHECK(synthesize_chime("cloud_chat", 96000).size() == 2 * synthesize_chime("cloud_chat", rate).size());
  CHECK(synthesize_chime("mute", rate).empty() && !can_chime("mute"));
  CHECK(synthesize_chime("cloud_chat", 0).empty());
}

}  // namespace

int main() {
  every_kind_stays_short_finite_and_in_range();
  the_same_request_gives_the_same_sound_and_variants_differ();
  packs_keep_their_character();
  a_release_is_lighter_than_a_press();
  cues_differ_and_fit_any_rate();
  chimes_are_short_bright_and_only_for_two_names();
  return crpp_test_result();
}
