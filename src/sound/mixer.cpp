#include "crpp/sound/mixer.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace crpp::sound {

namespace {

constexpr double seat_spacing_m = 1.5;
constexpr double head_radius_m = 0.0875, speed_of_sound = 343;
constexpr double distance_rolloff = 0.6;
constexpr double shadow_corner_hz = 1250, open_corner_hz = 18000;
constexpr double far_seat_corner_hz = 8000;  // air absorption at the far corners
constexpr float send_near = 0.25f, send_far = 0.55f;

// Freeverb, at 44.1 kHz (see the research note).
constexpr std::array<int, 8> comb_tuning{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr std::array<int, 4> allpass_tuning{556, 441, 341, 225};
constexpr int stereo_spread = 23;
constexpr float room = 0.3f, damp = 0.5f;
constexpr float comb_feedback = room * 0.28f + 0.7f, comb_damp = damp * 0.4f;
constexpr float allpass_feedback = 0.5f, reverb_input_gain = 0.015f, reverb_wet = 1.2f;

constexpr float silence_level = 2e-5f;
constexpr float clip_knee = 0.85f;

float one_pole(double corner_hz, std::uint32_t rate) noexcept {
  return static_cast<float>(std::exp(-2 * std::numbers::pi * corner_hz / rate));
}

// The value at a fractional position before the start of the recording, or after
// its end, is silence.
float read_at(std::span<const float> s, float position) noexcept {
  if (position < 0) return 0;
  const auto i = static_cast<std::size_t>(position);
  if (i >= s.size()) return 0;
  const float next = i + 1 < s.size() ? s[i + 1] : 0.0f;
  return s[i] + (next - s[i]) * (position - static_cast<float>(i));
}

}  // namespace

SeatMix seat_mix(std::size_t seat, std::uint32_t rate, const SpatialConfig& config) noexcept {
  if (!config.spatial) return {};
  const double column = static_cast<double>(seat % 4) - 1.5, row = static_cast<double>((seat / 4) % 4) - 1.5;
  const double x = column * seat_spacing_m, y = row * seat_spacing_m;
  const double distance = std::hypot(x, y);
  const double far_distance = std::hypot(1.5, 1.5) * seat_spacing_m;
  const double nearness = (distance - seat_spacing_m * std::hypot(0.5, 0.5)) /
                          (far_distance - seat_spacing_m * std::hypot(0.5, 0.5));
  const double side = distance > 0 ? x / distance : 0;  // -1 left, +1 right
  const double theta = std::asin(std::abs(side));

  // Leaning: at balance 100 and straight to one side, the amplitudes are 90:10.
  const double extent = std::clamp(config.balance, 0, 100) / 100.0;
  const double full_lean = 2 / std::numbers::pi * std::atan(9.0);
  const double pan = 0.5 + 0.5 * side * extent * (2 * full_lean - 1);
  const double angle = pan * std::numbers::pi / 2;
  const double distance_gain = 1 / (1 + (std::max(distance, 1.0) - 1) * distance_rolloff);

  // Woodworth: the far ear hears the sound late and through the head.
  const double itd = head_radius_m / speed_of_sound * (theta + std::sin(theta)) * extent * rate;
  const double far_corner = open_corner_hz + (shadow_corner_hz - open_corner_hz) * std::abs(side) * extent;
  const double air_corner = open_corner_hz + (far_seat_corner_hz - open_corner_hz) * nearness;
  const float near_lp = one_pole(air_corner, rate);
  const float far_lp = one_pole(std::min(air_corner, far_corner), rate);

  SeatMix mix;
  mix.left = static_cast<float>(std::cos(angle) * distance_gain);
  mix.right = static_cast<float>(std::sin(angle) * distance_gain);
  mix.delay_left = side > 0 ? static_cast<float>(itd) : 0;
  mix.delay_right = side < 0 ? static_cast<float>(itd) : 0;
  mix.lowpass_left = side > 0 ? far_lp : near_lp;
  mix.lowpass_right = side < 0 ? far_lp : near_lp;
  mix.send = send_near + (send_far - send_near) * static_cast<float>(nearness);
  return mix;
}

float soft_clip(float x) noexcept {
  const float a = std::abs(x);
  if (a <= clip_knee) return x;
  const float bent = clip_knee + (1 - clip_knee) * std::tanh((a - clip_knee) / (1 - clip_knee));
  return x < 0 ? -bent : bent;
}

void Mixer::prepare(std::uint32_t rate) {
  head_.store(0);
  tail_.store(0);
  voices_ = {};
  const double scale = rate / 44100.0;
  for (std::size_t ch = 0; ch < reverb_.size(); ++ch) {
    const int spread = ch == 0 ? 0 : stereo_spread;
    for (std::size_t i = 0; i < comb_tuning.size(); ++i) {
      reverb_[ch].combs[i].buffer.assign(static_cast<std::size_t>((comb_tuning[i] + spread) * scale), 0.0f);
      reverb_[ch].combs[i].index = 0;
      reverb_[ch].combs[i].store = 0;
    }
    for (std::size_t i = 0; i < allpass_tuning.size(); ++i) {
      reverb_[ch].allpasses[i].buffer.assign(static_cast<std::size_t>((allpass_tuning[i] + spread) * scale), 0.0f);
      reverb_[ch].allpasses[i].index = 0;
    }
  }
  master_ = master_target_.load();
  master_step_ = static_cast<float>(1 - std::exp(-1.0 / (0.02 * rate)));  // 20 ms
  steal_frames_ = static_cast<std::int32_t>(rate / 100);                  // 10 ms
  quiet_limit_ = rate / 10;
  quiet_frames_ = 0;
  silent_.store(true);
}

bool Mixer::trigger(std::span<const float> samples, const SeatMix& mix, float gain) noexcept {
  const std::size_t head = head_.load(std::memory_order_relaxed);
  const std::size_t next = (head + 1) % queue_size;
  if (next == tail_.load(std::memory_order_acquire)) return false;
  queue_[head] = {samples, mix, gain};
  silent_.store(false, std::memory_order_release);  // before the entry is visible
  head_.store(next, std::memory_order_release);
  return true;
}

void Mixer::take_triggers() noexcept {
  std::size_t tail = tail_.load(std::memory_order_relaxed);
  const std::size_t head = head_.load(std::memory_order_acquire);
  while (tail != head) {
    const Trigger& t = queue_[tail];
    Voice* slot = nullptr;
    Voice* oldest = nullptr;
    std::size_t active = 0;
    for (Voice& v : voices_) {
      if (!v.active) {
        if (!slot) slot = &v;
        continue;
      }
      ++active;
      if (v.fade < 0 && (!oldest || v.age < oldest->age)) oldest = &v;
    }
    // A sound must never be cut dead, so a full mixer drops the new one instead.
    if (active >= soft_voice_limit && oldest) oldest->fade = steal_frames_;
    if (slot) {
      slot->samples = t.samples;
      slot->mix = t.mix;
      slot->gain = t.gain;
      slot->state_left = slot->state_right = 0;
      slot->position = 0;
      slot->age = clock_++;
      slot->fade = -1;
      slot->active = true;
    }
    tail = (tail + 1) % queue_size;
  }
  tail_.store(tail, std::memory_order_release);
}

float Mixer::reverb_channel(ReverbChannel& channel, float input) noexcept {
  float out = 0;
  for (Comb& c : channel.combs) {
    const float y = c.buffer[c.index];
    c.store = y * (1 - comb_damp) + c.store * comb_damp;
    c.buffer[c.index] = input + c.store * comb_feedback;
    if (++c.index == c.buffer.size()) c.index = 0;
    out += y;
  }
  for (Allpass& a : channel.allpasses) {
    const float delayed = a.buffer[a.index];
    a.buffer[a.index] = out + delayed * allpass_feedback;
    if (++a.index == a.buffer.size()) a.index = 0;
    out = delayed - out;
  }
  return out;
}

void Mixer::clear_reverb() noexcept {
  for (ReverbChannel& channel : reverb_) {
    for (Comb& c : channel.combs) {
      std::ranges::fill(c.buffer, 0.0f);
      c.store = 0;
    }
    for (Allpass& a : channel.allpasses) std::ranges::fill(a.buffer, 0.0f);
  }
}

void Mixer::render(std::span<float> stereo) noexcept {
  const bool queued = head_.load(std::memory_order_acquire) != tail_.load(std::memory_order_relaxed);
  if (!queued && silent_.load(std::memory_order_acquire)) {
    std::ranges::fill(stereo, 0.0f);
    return;
  }
  if (queued) take_triggers();

  const std::size_t frames = stereo.size() / 2;
  const float master_target = master_target_.load(std::memory_order_relaxed);
  bool any_voice = false;
  for (std::size_t f = 0; f < frames; ++f) {
    float left = 0, right = 0, send = 0;
    for (Voice& v : voices_) {
      if (!v.active) continue;
      any_voice = true;
      const auto position = static_cast<float>(v.position);
      float l = read_at(v.samples, position - v.mix.delay_left);
      float r = read_at(v.samples, position - v.mix.delay_right);
      v.state_left = l + (v.state_left - l) * v.mix.lowpass_left;
      v.state_right = r + (v.state_right - r) * v.mix.lowpass_right;
      float gain = v.gain;
      if (v.fade >= 0) gain *= static_cast<float>(v.fade) / static_cast<float>(steal_frames_);
      left += v.state_left * v.mix.left * gain;
      right += v.state_right * v.mix.right * gain;
      send += (v.state_left + v.state_right) * 0.5f * v.mix.send * gain;
      ++v.position;
      const float tail = std::max(v.mix.delay_left, v.mix.delay_right) + 2;
      if (position >= static_cast<float>(v.samples.size()) + tail || v.fade == 0) v.active = false;
      else if (v.fade > 0) --v.fade;
    }
    const float wet_left = reverb_channel(reverb_[0], send * reverb_input_gain) * reverb_wet;
    const float wet_right = reverb_channel(reverb_[1], send * reverb_input_gain) * reverb_wet;
    master_ += (master_target - master_) * master_step_;
    const float out_left = (left + wet_left) * master_;
    const float out_right = (right + wet_right) * master_;
    stereo[f * 2] = soft_clip(out_left);
    stereo[f * 2 + 1] = soft_clip(out_right);
    if (std::abs(wet_left) < silence_level && std::abs(wet_right) < silence_level && left == 0 && right == 0) {
      if (quiet_frames_ < quiet_limit_) ++quiet_frames_;
    } else {
      quiet_frames_ = 0;
    }
  }

  // Ringing out has ended: from now on, no work at all.
  const bool voices_left = any_voice && std::ranges::any_of(voices_, [](const Voice& v) { return v.active; });
  if (!voices_left && quiet_frames_ >= quiet_limit_) {
    clear_reverb();
    quiet_frames_ = 0;
    silent_.store(true, std::memory_order_release);
    // A trigger may have marked it not silent just before; it sees itself queued.
    if (head_.load(std::memory_order_acquire) != tail_.load(std::memory_order_relaxed)) {
      silent_.store(false, std::memory_order_release);
    }
  }
}

}  // namespace crpp::sound
