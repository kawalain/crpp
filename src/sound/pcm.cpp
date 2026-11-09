#include "crpp/sound/pcm.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numeric>
#include <numbers>

namespace crpp::sound {

namespace {

constexpr std::uint16_t format_pcm = 1, format_float = 3, format_extensible = 0xFFFE;
constexpr std::uint32_t min_rate = 8000, max_rate = 192000;
// The rates recordings come in. The resampler keeps a row of its kernel per phase of the ratio of
// two rates, which is a handful between any two of these and tens of thousands for one a step off.
constexpr std::array<std::uint32_t, 13> known_rates{8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000, 176400, 192000};

std::uint16_t u16(const std::uint8_t* p) noexcept { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }
std::uint32_t u32(const std::uint8_t* p) noexcept {
  return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 | static_cast<std::uint32_t>(p[2]) << 16 |
         static_cast<std::uint32_t>(p[3]) << 24;
}

// One channel's value at p, normalised by the container size.
float read_sample(const std::uint8_t* p, std::uint16_t format, std::uint16_t bits) noexcept {
  if (format == format_float) {
    float v;
    std::memcpy(&v, p, sizeof v);
    return std::isfinite(v) ? v : 0.0f;
  }
  switch (bits) {
    case 8: return (static_cast<float>(p[0]) - 128.0f) / 128.0f;
    case 16: return static_cast<float>(static_cast<std::int16_t>(u16(p))) / 32768.0f;
    case 24: {
      const std::uint32_t raw = static_cast<std::uint32_t>(p[0]) << 8 | static_cast<std::uint32_t>(p[1]) << 16 |
                                static_cast<std::uint32_t>(p[2]) << 24;
      return static_cast<float>(static_cast<std::int32_t>(raw) >> 8) / 8388608.0f;
    }
    default: return static_cast<float>(static_cast<double>(static_cast<std::int32_t>(u32(p))) / 2147483648.0);
  }
}

double bessel_i0(double x) noexcept {
  double sum = 1, term = 1;
  for (int k = 1; k < 64; ++k) {
    term *= (x / 2) / k;
    const double squared = term * term;
    sum += squared;
    if (squared < sum * 1e-16) break;
  }
  return sum;
}

double sinc(double x) noexcept {
  if (std::abs(x) < 1e-9) return 1;
  const double a = std::numbers::pi * x;
  return std::sin(a) / a;
}

}  // namespace

std::expected<Pcm, WavError> parse_wav(std::span<const std::uint8_t> bytes) {
  if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
    return std::unexpected(WavError::NotWav);
  }
  std::uint16_t format = 0, channels = 0, bits = 0, block = 0;
  std::uint32_t rate = 0;
  bool have_format = false;
  std::span<const std::uint8_t> data;
  std::size_t pos = 12;
  while (pos + 8 <= bytes.size()) {
    const std::uint8_t* header = bytes.data() + pos;
    const std::size_t room = bytes.size() - pos - 8;
    const std::size_t size = std::min<std::size_t>(u32(header + 4), room);
    const std::uint8_t* body = header + 8;
    if (std::memcmp(header, "fmt ", 4) == 0) {
      if (size < 16) return std::unexpected(WavError::BadFormat);
      format = u16(body);
      channels = u16(body + 2);
      rate = u32(body + 4);
      block = u16(body + 12);
      bits = u16(body + 14);
      if (format == format_extensible) {
        if (size < 40 || u16(body + 16) < 22) return std::unexpected(WavError::BadFormat);
        format = u16(body + 24);  // the first bytes of the SubFormat GUID
      }
      have_format = true;
    } else if (std::memcmp(header, "data", 4) == 0) {
      data = bytes.subspan(pos + 8, size);
    }
    pos += 8 + size + (size & 1);  // chunks are padded to an even size
  }
  if (!have_format) return std::unexpected(WavError::BadFormat);
  // Real recordings lie between telephone and studio rates. Outside them the resampler's kernel and
  // output, sized by the ratio, could ask for more memory than there is.
  if (channels == 0 || rate < min_rate || rate > max_rate || block != channels * bits / 8) return std::unexpected(WavError::BadFormat);
  const bool pcm_ok = format == format_pcm && (bits == 8 || bits == 16 || bits == 24 || bits == 32);
  const bool float_ok = format == format_float && bits == 32;
  if (!pcm_ok && !float_ok) return std::unexpected(WavError::Unsupported);
  if (std::ranges::find(known_rates, rate) == known_rates.end()) return std::unexpected(WavError::Unsupported);
  if (data.size() < block) return std::unexpected(WavError::NoData);
  if (data.size() / block > std::uint64_t{rate} * max_recording_seconds) return std::unexpected(WavError::TooLong);

  Pcm out;
  out.rate = rate;
  const std::size_t frames = data.size() / block;
  out.samples.resize(frames);
  const std::size_t step = bits / 8;
  for (std::size_t f = 0; f < frames; ++f) {
    float sum = 0;
    for (std::uint16_t c = 0; c < channels; ++c) sum += read_sample(data.data() + f * block + c * step, format, bits);
    out.samples[f] = sum / static_cast<float>(channels);
  }
  return out;
}

Resampler::Resampler(std::uint32_t in_rate, std::uint32_t out_rate) {
  const std::uint32_t g = std::gcd(in_rate, out_rate);
  in_step_ = in_rate / g;
  out_step_ = out_rate / g;
  if (in_step_ == out_step_) return;

  // 96 dB of rejection with a 2 kHz transition: see the filter design note.
  constexpr double attenuation = 96, transition_hz = 2000;
  const double nyquist_rate = std::min(in_rate, out_rate);
  const double beta = 0.1102 * (attenuation - 8.7);
  const double dw = 2 * std::numbers::pi * transition_hz / nyquist_rate;
  const double half = (attenuation - 8) / (2.285 * dw) / 2;  // in samples of the lower rate
  const double c = nyquist_rate / in_rate;                   // cutoff, relative to the input rate
  const double half_in = half / c;
  const double i0_beta = bessel_i0(beta);

  taps_ = static_cast<std::size_t>(2 * std::ceil(half_in) + 1);
  kernel_.resize(out_step_ * taps_);
  first_.resize(out_step_);
  for (std::uint32_t phase = 0; phase < out_step_; ++phase) {
    // The output sample falls this far after its base input sample.
    const double frac = static_cast<double>(std::uint64_t{phase} * in_step_ % out_step_) / out_step_;
    const int first = static_cast<int>(std::ceil(frac - half_in));
    first_[phase] = first;
    double sum = 0;
    for (std::size_t t = 0; t < taps_; ++t) {
      const double d = frac - (first + static_cast<int>(t));
      const double r = d / half_in;
      const double window = std::abs(r) >= 1 ? 0 : bessel_i0(beta * std::sqrt(1 - r * r)) / i0_beta;
      const double weight = c * sinc(c * d) * window;
      kernel_[phase * taps_ + t] = static_cast<float>(weight);
      sum += weight;
    }
    for (std::size_t t = 0; t < taps_; ++t) kernel_[phase * taps_ + t] = static_cast<float>(kernel_[phase * taps_ + t] / sum);
  }
}

std::vector<float> Resampler::run(std::span<const float> in) const {
  if (in_step_ == out_step_) return {in.begin(), in.end()};
  if (in.empty()) return {};
  const auto n = static_cast<std::int64_t>(in.size());
  const std::int64_t frames = (n * out_step_ + in_step_ - 1) / in_step_;
  std::vector<float> out(static_cast<std::size_t>(frames));
  for (std::int64_t m = 0; m < frames; ++m) {
    const std::int64_t scaled = m * in_step_;
    const std::int64_t base = scaled / out_step_;
    const auto phase = static_cast<std::size_t>(m % out_step_);
    const float* weights = kernel_.data() + phase * taps_;
    const std::int64_t first = base + first_[phase];
    const std::int64_t from = std::max<std::int64_t>(first, 0);
    const std::int64_t to = std::min<std::int64_t>(first + static_cast<std::int64_t>(taps_), n);
    float sum = 0;
    for (std::int64_t k = from; k < to; ++k) sum += in[static_cast<std::size_t>(k)] * weights[k - first];
    out[static_cast<std::size_t>(m)] = sum;
  }
  return out;
}

}  // namespace crpp::sound
