#include "crpp/sound/mixer.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "check.hpp"

using namespace crpp::sound;

namespace {

constexpr std::uint32_t rate = 48000;

std::unique_ptr<Mixer> ready_mixer() {
  auto mixer = std::make_unique<Mixer>();
  mixer->prepare(rate);
  return mixer;
}

std::vector<float> click(std::size_t frames = 2000) {
  std::vector<float> s(frames);
  for (std::size_t i = 0; i < frames; ++i) s[i] = 0.5f * std::sin(static_cast<float>(i) * 0.05f) * std::exp(-static_cast<float>(i) / 500.0f);
  return s;
}

std::vector<float> render(Mixer& mixer, std::size_t frames) {
  std::vector<float> out(frames * 2);
  mixer.render(out);
  return out;
}

float peak(std::span<const float> s, std::size_t channel) {
  float p = 0;
  for (std::size_t i = channel; i < s.size(); i += 2) p = std::max(p, std::abs(s[i]));
  return p;
}

void silence_does_nothing() {
  auto mixer = ready_mixer();
  CHECK(mixer->idle());
  const auto out = render(*mixer, 256);
  CHECK(peak(out, 0) == 0 && peak(out, 1) == 0);
  CHECK(mixer->idle());
}

void a_voice_sounds_and_rings_out() {
  auto mixer = ready_mixer();
  const auto sample = click();
  CHECK(mixer->trigger(sample, own_mix(), 1));
  CHECK(!mixer->idle());
  const auto out = render(*mixer, 2000);
  CHECK(peak(out, 0) > 0.1f);
  CHECK(!mixer->idle());
  // Without a send there is no reverb, so it ends soon after the voice.
  render(*mixer, 12000);
  CHECK(mixer->idle());
}

void desks_lean_to_their_side() {
  const SpatialConfig config;
  const DeskMix far_left = desk_mix(4, rate, config);  // column 0
  const DeskMix far_right = desk_mix(7, rate, config);  // column 3
  CHECK(far_left.left > far_left.right);
  CHECK(far_right.right > far_right.left);
  CHECK(std::abs(far_left.left - far_right.right) < 1e-5f);
  // The far ear hears later and duller.
  CHECK(far_left.delay_right > 0 && far_left.delay_left == 0);
  CHECK(far_right.delay_left > 0 && far_right.delay_right == 0);
  CHECK(far_left.lowpass_right > far_left.lowpass_left);
  // Woodworth's maximum is about 656 us.
  CHECK(far_left.delay_right < 0.00066f * rate);
}

void distance_lowers_the_level_and_raises_the_reverb() {
  const SpatialConfig config;
  const DeskMix near_desk = desk_mix(5, rate, config);    // an inner desk
  const DeskMix corner_desk = desk_mix(0, rate, config);  // a corner
  const auto power = [](const DeskMix& m) { return m.left * m.left + m.right * m.right; };
  CHECK(power(near_desk) > power(corner_desk));
  CHECK(corner_desk.send > near_desk.send);
}

void balance_controls_the_lean() {
  const DeskMix none = desk_mix(4, rate, {true, 0});
  CHECK(std::abs(none.left - none.right) < 1e-5f);
  CHECK(none.delay_left == 0 && none.delay_right == 0);
  const DeskMix full = desk_mix(4, rate, {true, 90});
  // A desk straight to the side would be 90:10; this one is a little off axis.
  CHECK(full.left / full.right > 3);
  CHECK(full.left / full.right < 9.01f);
  // The default, 60:40 straight to the side.
  const DeskMix label = desk_mix(4, rate, {true, 60});
  CHECK(label.left / label.right > 1.3f);
  CHECK(label.left / label.right < 1.51f);
}

void without_spatial_audio_everything_is_centred() {
  const DeskMix mix = desk_mix(0, rate, {false, 60});
  CHECK(mix.left == mix.right);
  CHECK(mix.send == 0);
  CHECK(mix.delay_left == 0 && mix.delay_right == 0);
}

void every_desk_is_sane() {
  for (std::size_t desk = 0; desk < desk_count; ++desk) {
    const DeskMix m = desk_mix(desk, rate, {});
    CHECK(m.left > 0 && m.right > 0 && m.left < 1 && m.right < 1);
    CHECK(m.lowpass_left >= 0 && m.lowpass_left < 1 && m.lowpass_right >= 0 && m.lowpass_right < 1);
    CHECK(m.send > 0 && m.send < 1);
  }
}

void reverb_rings_on_then_stops() {
  auto mixer = ready_mixer();
  const auto sample = click();
  CHECK(mixer->trigger(sample, desk_mix(0, rate, {}), 1));
  render(*mixer, 2200);
  const auto tail = render(*mixer, 4800);
  CHECK(peak(tail, 0) > 1e-4f);
  CHECK(!mixer->idle());
  for (int i = 0; i < 400 && !mixer->idle(); ++i) render(*mixer, 2400);
  CHECK(mixer->idle());
  const auto after = render(*mixer, 256);
  CHECK(peak(after, 0) == 0 && peak(after, 1) == 0);
}

void the_output_stays_in_range() {
  auto mixer = ready_mixer();
  const std::vector<float> loud(4000, 1.0f);
  for (int i = 0; i < 40; ++i) CHECK(mixer->trigger(loud, own_mix(), 1));
  const auto out = render(*mixer, 3000);
  for (const float v : out) {
    CHECK(std::isfinite(v));
    CHECK(std::abs(v) <= 1.0f);
  }
  CHECK(peak(out, 0) > 0.9f);
}

void soft_clip_is_smooth() {
  CHECK(soft_clip(0.5f) == 0.5f);
  CHECK(soft_clip(-0.5f) == -0.5f);
  CHECK(soft_clip(0.85f) == 0.85f);
  CHECK(std::abs(soft_clip(0.8501f) - 0.8501f) < 1e-3f);
  CHECK(soft_clip(10) <= 1 && soft_clip(10) > 0.99f);
  CHECK(soft_clip(-10) >= -1);
  CHECK(soft_clip(1.0f) > soft_clip(0.9f));
}

void more_than_the_limit_never_fails() {
  auto mixer = ready_mixer();
  const auto sample = click(20000);
  for (int round = 0; round < 8; ++round) {
    for (int i = 0; i < 40; ++i) (void)mixer->trigger(sample, own_mix(), 0.01f);
    const auto out = render(*mixer, 512);
    for (const float v : out) CHECK(std::isfinite(v));
  }
}

void a_full_queue_refuses() {
  auto mixer = ready_mixer();
  const auto sample = click();
  int accepted = 0;
  for (int i = 0; i < 1000; ++i) accepted += mixer->trigger(sample, own_mix(), 0) ? 1 : 0;
  CHECK(accepted == 255);
  render(*mixer, 16);
  CHECK(mixer->trigger(sample, own_mix(), 0));
}

void master_gain_fades() {
  auto mixer = ready_mixer();
  const std::vector<float> steady(48000, 0.5f);
  CHECK(mixer->trigger(steady, own_mix(), 1));
  const auto loud = render(*mixer, 4800);
  mixer->set_master(0);
  render(*mixer, 9600);  // well past the 20 ms smoothing
  const auto quiet = render(*mixer, 480);
  CHECK(peak(loud, 0) > 0.3f);
  CHECK(peak(quiet, 0) < 1e-3f);
}

void preparing_again_forgets_everything() {
  auto mixer = ready_mixer();
  const auto sample = click();
  CHECK(mixer->trigger(sample, own_mix(), 1));
  mixer->prepare(44100);
  CHECK(mixer->idle());
  const auto out = render(*mixer, 256);
  CHECK(peak(out, 0) == 0);
}

}  // namespace

int main() {
  silence_does_nothing();
  a_voice_sounds_and_rings_out();
  desks_lean_to_their_side();
  distance_lowers_the_level_and_raises_the_reverb();
  balance_controls_the_lean();
  without_spatial_audio_everything_is_centred();
  every_desk_is_sane();
  reverb_rings_on_then_stops();
  the_output_stays_in_range();
  soft_clip_is_smooth();
  more_than_the_limit_never_fails();
  a_full_queue_refuses();
  master_gain_fades();
  preparing_again_forgets_everything();
  return crpp_test_result();
}
