#include "crpp/sound/mixer.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace crpp::sound {

namespace {

constexpr double desk_spacing_m = 1.5;
constexpr double head_radius_m = 0.0875, speed_of_sound = 343;
constexpr double distance_rolloff = 0.6;
// Starting points to tune by ear, as no source found maps level difference to place: 6 dB
// at the default, up to 19 dB, about the 9:1 the slider's end has always meant.
constexpr double difference_at_60_db = 6, difference_db_per_step = 13.0 / 30;
constexpr double shadow_corner_hz = 1250, open_corner_hz = 18000;
constexpr double far_desk_corner_hz = 8000;  // air absorption at the far corners
constexpr float send_near = 0.25f, send_far = 0.55f;
constexpr float send_flat = (send_near + send_far) / 2;  // every Desk, without Virtual surround

// Freeverb, at 44.1 kHz (see the research note).
constexpr std::array<int, 8> comb_tuning{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr std::array<int, 4> allpass_tuning{556, 441, 341, 225};
constexpr int stereo_spread = 23;
constexpr float allpass_feedback = 0.5f, reverb_input_gain = 0.015f;

// The combs average 31 ms, so a feedback g dies away 60 dB in about -0.094 / log10(g) s:
// 0.9 s for Large and 0.3 s for Office, whose level also stays well under the Desks'.
constexpr float large_room = 0.3f, large_damp = 0.5f;
constexpr ReverbTuning large_tuning{large_room * 0.28f + 0.7f, large_damp * 0.4f, 1.2f};
constexpr ReverbTuning office_tuning{0.49f, 0.3f, 0.6f};

constexpr ReverbTuning tuning_of(Reverb reverb) noexcept { return reverb == Reverb::Large ? large_tuning : office_tuning; }

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

double desk_x(std::size_t desk) noexcept { return (static_cast<double>(desk % 4) - 1.5) * desk_spacing_m; }
double desk_y(std::size_t desk) noexcept { return (static_cast<double>((desk / 4) % 4) - 1.5) * desk_spacing_m; }

// -1 straight to the left, +1 straight to the right.
double desk_side(std::size_t desk) noexcept {
  const double distance = std::hypot(desk_x(desk), desk_y(desk));
  return distance > 0 ? desk_x(desk) / distance : 0;
}

}  // namespace

std::size_t farthest_desks(const std::array<bool, desk_count>& taken, std::array<std::size_t, desk_count>& out) noexcept {
  constexpr double same = 1e-9;
  double best = -1;
  std::size_t count = 0;
  for (std::size_t desk = 0; desk < desk_count; ++desk) {
    if (taken[desk]) continue;
    double gap = std::abs(desk_side(desk));
    for (std::size_t other = 0; other < desk_count; ++other) {
      if (taken[other]) gap = std::min(gap, std::abs(desk_side(desk) - desk_side(other)));
    }
    if (gap > best + same) {
      best = gap;
      count = 0;
    }
    if (gap > best - same) out[count++] = desk;
  }
  if (count > 0) return count;
  for (std::size_t desk = 0; desk < desk_count; ++desk) out[desk] = desk;
  return desk_count;
}

DeskMix desk_mix(std::size_t desk, std::uint32_t rate, const SpatialConfig& config) noexcept {
  const bool reverb = config.reverb != Reverb::Off;
  if (!config.spatial) {
    DeskMix flat;
    flat.send = reverb ? send_flat : 0;
    return flat;
  }
  const double distance = std::hypot(desk_x(desk), desk_y(desk));
  const double far_distance = std::hypot(1.5, 1.5) * desk_spacing_m;
  const double nearness = (distance - desk_spacing_m * std::hypot(0.5, 0.5)) /
                          (far_distance - desk_spacing_m * std::hypot(0.5, 0.5));
  const double side = desk_side(desk);
  const double theta = std::asin(std::abs(side));

  // Leaning: straight to one side, the ears differ by this much.
  const int balance = std::clamp(config.balance, 0, 100);
  const double extent = balance / 100.0;
  const double difference_db = balance > 50 ? difference_at_60_db + (balance - 60) * difference_db_per_step : 0;
  const double full_lean = 2 / std::numbers::pi * std::atan(std::pow(10.0, difference_db / 20));
  const double pan = 0.5 + 0.5 * side * (2 * full_lean - 1);
  const double angle = pan * std::numbers::pi / 2;
  const double distance_gain = 1 / (1 + (std::max(distance, 1.0) - 1) * distance_rolloff);

  // Woodworth: the far ear hears the sound late and through the head.
  const double itd = head_radius_m / speed_of_sound * (theta + std::sin(theta)) * extent * rate;
  const double far_corner = open_corner_hz + (shadow_corner_hz - open_corner_hz) * std::abs(side) * extent;
  const double air_corner = open_corner_hz + (far_desk_corner_hz - open_corner_hz) * nearness;
  const float near_lp = one_pole(air_corner, rate);
  const float far_lp = one_pole(std::min(air_corner, far_corner), rate);

  DeskMix mix;
  mix.left = static_cast<float>(std::cos(angle) * distance_gain);
  mix.right = static_cast<float>(std::sin(angle) * distance_gain);
  mix.delay_left = side > 0 ? static_cast<float>(itd) : 0;
  mix.delay_right = side < 0 ? static_cast<float>(itd) : 0;
  mix.lowpass_left = side > 0 ? far_lp : near_lp;
  mix.lowpass_right = side < 0 ? far_lp : near_lp;
  mix.send = reverb ? send_near + (send_far - send_near) * static_cast<float>(nearness) : 0;
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
  quiet_frames_ = tail_quiet_frames_ = 0;
  ringing_ = false;
  tuning_ = tuning_of(reverb_mode_.load());
  wet_ = tuning_.wet;
  silent_.store(true);
}

bool Mixer::trigger(std::span<const float> samples, const DeskMix& mix, float gain) noexcept {
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
      if (t.mix.send > 0) ringing_ = true;
    }
    tail = (tail + 1) % queue_size;
  }
  tail_.store(tail, std::memory_order_release);
}

float Mixer::reverb_channel(ReverbChannel& channel, float input) noexcept {
  float out = 0;
  for (Comb& c : channel.combs) {
    const float y = c.buffer[c.index];
    c.store = y * (1 - tuning_.damp) + c.store * tuning_.damp;
    c.buffer[c.index] = input + c.store * tuning_.feedback;
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
  // Off leaves the last tuning, so what is ringing dies away as it was going to.
  if (const Reverb mode = reverb_mode_.load(std::memory_order_relaxed); mode != Reverb::Off) tuning_ = tuning_of(mode);
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
    float wet_left = 0, wet_right = 0;
    if (ringing_) {
      wet_ += (tuning_.wet - wet_) * master_step_;
      wet_left = reverb_channel(reverb_[0], send * reverb_input_gain) * wet_;
      wet_right = reverb_channel(reverb_[1], send * reverb_input_gain) * wet_;
      const bool tail_gone = send == 0 && std::abs(wet_left) < silence_level && std::abs(wet_right) < silence_level;
      tail_quiet_frames_ = tail_gone ? tail_quiet_frames_ + 1 : 0;
      if (tail_quiet_frames_ >= quiet_limit_) {
        clear_reverb();
        ringing_ = false;
        tail_quiet_frames_ = 0;
      }
    }
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
  if (!voices_left && !ringing_ && quiet_frames_ >= quiet_limit_) {
    quiet_frames_ = 0;
    silent_.store(true, std::memory_order_release);
    // A trigger may have marked it not silent just before; it sees itself queued.
    if (head_.load(std::memory_order_acquire) != tail_.load(std::memory_order_relaxed)) {
      silent_.store(false, std::memory_order_release);
    }
  }
}

}  // namespace crpp::sound
