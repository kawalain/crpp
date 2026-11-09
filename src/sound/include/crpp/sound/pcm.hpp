#pragma once
// Reading WAV files and converting them to the mix rate, once, when loaded.
// Pure; spec and sources in docs/research/audio-output.md.
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace crpp::sound {

// Unsupported: a format, or a rate recordings do not come in. TooLong: more than max_recording_seconds.
enum class WavError : std::uint8_t { NotWav, BadFormat, Unsupported, NoData, TooLong };

// A recording is a key or a click. The limit keeps a pack from anyone from asking for memory by the minute.
inline constexpr std::uint32_t max_recording_seconds = 30;

// Every recording plays from one point in space, so it is kept in mono.
struct Pcm {
  std::uint32_t rate = 0;
  std::vector<float> samples;
};

[[nodiscard]] std::expected<Pcm, WavError> parse_wav(std::span<const std::uint8_t> bytes);

// Band-limited conversion between two fixed rates with a Kaiser-windowed sinc.
// The kernel is computed once per output phase, so converting is only a sum.
class Resampler {
 public:
  Resampler(std::uint32_t in_rate, std::uint32_t out_rate);
  [[nodiscard]] std::vector<float> run(std::span<const float> in) const;

 private:
  std::uint32_t in_step_ = 1;   // in_rate / gcd
  std::uint32_t out_step_ = 1;  // out_rate / gcd
  std::size_t taps_ = 0;
  std::vector<float> kernel_;  // out_step_ rows of taps_ weights, each summing to 1
  std::vector<int> first_;     // per phase: first tap's offset from the base input sample
};

}  // namespace crpp::sound
