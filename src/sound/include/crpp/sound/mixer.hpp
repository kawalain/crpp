#pragma once
// The realtime part: short mono recordings placed in a virtual office, summed
// with a shared reverb. render() takes no locks, allocates nothing and does no
// work while nothing is sounding. Design and sources:
// docs/research/spatial-reverb.md. Levels are tuned by ear with crpp_soundtest.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace crpp::sound {

inline constexpr std::size_t desk_count = 16;  // 4 x 4, with the listener in the middle

// Per-ear parameters of one position, precomputed since positions never move.
struct DeskMix {
  float left = 0.7071f, right = 0.7071f;      // constant-power pan times distance gain
  float delay_left = 0, delay_right = 0;      // samples
  float lowpass_left = 0, lowpass_right = 0;  // one-pole coefficient; 0 passes everything
  float send = 0;                             // level into the reverb
};

struct SpatialConfig {
  bool spatial = true;
  // How strongly a Desk to one side leans, on the slider's scale from 55 to 90: straight
  // to the side, 60 is 6 dB between the ears and 90 is 19 dB. 50 or less does not lean;
  // 0 also drops the other side cues.
  int balance = 60;
};

// Where a sound of this member plays from. Without spatial audio, the middle.
[[nodiscard]] DeskMix desk_mix(std::size_t desk, std::uint32_t rate, const SpatialConfig& config) noexcept;
// In the listener's own head: for their own sounds.
[[nodiscard]] constexpr DeskMix own_mix() noexcept { return {}; }

// The Desks a newcomer may get: the free ones whose direction is farthest from the
// directions already taken and from straight ahead, so that the first few members can
// be told apart. Fills `out` and returns how many; with none free, every Desk.
[[nodiscard]] std::size_t farthest_desks(const std::array<bool, desk_count>& taken,
                                         std::array<std::size_t, desk_count>& out) noexcept;

// Keeps a sum inside full scale: unchanged up to 0.85, then a smooth bend.
[[nodiscard]] float soft_clip(float x) noexcept;

class Mixer {
 public:
  // Allocates for the rate; the stream must be stopped. Forgets all sound.
  void prepare(std::uint32_t rate);

  // Producer side, one thread at a time. `samples` must outlive the voice; false
  // if too many triggers are queued. `gain` applies to this voice only.
  bool trigger(std::span<const float> samples, const DeskMix& mix, float gain) noexcept;
  void set_master(float gain) noexcept { master_target_.store(gain, std::memory_order_relaxed); }
  // True when nothing is queued, playing or ringing out.
  [[nodiscard]] bool idle() const noexcept {
    return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire) &&
           silent_.load(std::memory_order_acquire);
  }

  // Render side: fills interleaved stereo frames.
  void render(std::span<float> stereo) noexcept;

 private:
  struct Voice {
    std::span<const float> samples;
    DeskMix mix;
    float gain = 0;
    float state_left = 0, state_right = 0;
    std::uint32_t position = 0;
    std::uint32_t age = 0;
    std::int32_t fade = -1;  // frames left of a fade-out, or -1
    bool active = false;
  };
  struct Trigger {
    std::span<const float> samples;
    DeskMix mix;
    float gain = 0;
  };
  struct Comb {
    std::vector<float> buffer;
    std::size_t index = 0;
    float store = 0;
  };
  struct Allpass {
    std::vector<float> buffer;
    std::size_t index = 0;
  };
  struct ReverbChannel {
    std::array<Comb, 8> combs;
    std::array<Allpass, 4> allpasses;
  };

  void take_triggers() noexcept;
  [[nodiscard]] float reverb_channel(ReverbChannel& channel, float input) noexcept;
  void clear_reverb() noexcept;

  static constexpr std::size_t max_voices = 56;
  static constexpr std::size_t soft_voice_limit = 48;  // over this, the oldest fades out
  static constexpr std::size_t queue_size = 256;

  std::array<Trigger, queue_size> queue_{};
  std::atomic<std::size_t> head_{0}, tail_{0};
  std::array<Voice, max_voices> voices_{};
  std::array<ReverbChannel, 2> reverb_;
  std::atomic<float> master_target_{1};
  float master_ = 1, master_step_ = 0.001f;
  std::int32_t steal_frames_ = 256;
  std::uint32_t quiet_frames_ = 0, quiet_limit_ = 4800;
  std::uint32_t clock_ = 0;
  std::atomic<bool> silent_{true};
};

}  // namespace crpp::sound
