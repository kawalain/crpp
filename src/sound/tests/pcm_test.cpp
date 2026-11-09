#include "crpp/sound/pcm.hpp"

#include <cmath>
#include <cstring>
#include <numbers>
#include <string>
#include <vector>

#include "check.hpp"

using namespace crpp::sound;

namespace {

void put16(std::vector<std::uint8_t>& out, unsigned v) {
  out.push_back(static_cast<std::uint8_t>(v));
  out.push_back(static_cast<std::uint8_t>(v >> 8));
}
void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  put16(out, v & 0xFFFF);
  put16(out, v >> 16);
}
void tag(std::vector<std::uint8_t>& out, const char* t) { out.insert(out.end(), t, t + 4); }

std::vector<std::uint8_t> wav(unsigned format, unsigned channels, std::uint32_t rate, unsigned bits,
                              const std::vector<std::uint8_t>& data, const std::string& extra_chunk = {}) {
  std::vector<std::uint8_t> out;
  tag(out, "RIFF");
  put32(out, 0);
  tag(out, "WAVE");
  if (!extra_chunk.empty()) {
    tag(out, "LIST");
    put32(out, static_cast<std::uint32_t>(extra_chunk.size()));
    out.insert(out.end(), extra_chunk.begin(), extra_chunk.end());
    if (extra_chunk.size() % 2) out.push_back(0);
  }
  tag(out, "fmt ");
  put32(out, 16);
  put16(out, format);
  put16(out, channels);
  put32(out, rate);
  put32(out, rate * channels * bits / 8);
  put16(out, channels * bits / 8);
  put16(out, bits);
  tag(out, "data");
  put32(out, static_cast<std::uint32_t>(data.size()));
  out.insert(out.end(), data.begin(), data.end());
  return out;
}

bool near(float a, float b, float tolerance = 1e-4f) { return std::abs(a - b) < tolerance; }

void reads_each_sample_format() {
  auto p16 = parse_wav(wav(1, 1, 44100, 16, {0x00, 0x40, 0x00, 0x80}));
  CHECK(p16.has_value());
  CHECK(p16->rate == 44100);
  CHECK(p16->samples.size() == 2);
  CHECK(near(p16->samples[0], 0.5f) && near(p16->samples[1], -1.0f));

  auto p8 = parse_wav(wav(1, 1, 8000, 8, {192, 128}));
  CHECK(p8 && near(p8->samples[0], 0.5f) && near(p8->samples[1], 0));

  auto p24 = parse_wav(wav(1, 1, 8000, 24, {0x00, 0x00, 0xC0}));
  CHECK(p24 && near(p24->samples[0], -0.5f));

  auto p32 = parse_wav(wav(1, 1, 8000, 32, {0x00, 0x00, 0x00, 0x40}));
  CHECK(p32 && near(p32->samples[0], 0.5f));

  const float value = 0.25f;
  std::vector<std::uint8_t> bytes(4);
  std::memcpy(bytes.data(), &value, 4);
  auto pf = parse_wav(wav(3, 1, 8000, 32, bytes));
  CHECK(pf && near(pf->samples[0], 0.25f));
}

void stereo_becomes_mono() {
  auto p = parse_wav(wav(1, 2, 44100, 16, {0x00, 0x40, 0x00, 0x00}));
  CHECK(p && p->samples.size() == 1);
  CHECK(p && near(p->samples[0], 0.25f));
}

void skips_unknown_chunks_with_padding() {
  auto p = parse_wav(wav(1, 1, 44100, 16, {0x00, 0x40}, "abc"));
  CHECK(p && p->samples.size() == 1 && near(p->samples[0], 0.5f));
}

void rejects_bad_files() {
  CHECK(parse_wav({}).error() == WavError::NotWav);
  const std::vector<std::uint8_t> text{'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd', '!'};
  CHECK(parse_wav(text).error() == WavError::NotWav);
  CHECK(parse_wav(wav(1, 0, 44100, 16, {0, 0})).error() == WavError::BadFormat);
  CHECK(parse_wav(wav(1, 1, 0, 16, {0, 0})).error() == WavError::BadFormat);
  // A pack is data from anywhere: a rate far from real ones would size the resampler past memory.
  CHECK(parse_wav(wav(1, 1, 1, 16, {0, 0})).error() == WavError::BadFormat);
  CHECK(parse_wav(wav(1, 1, 7999, 16, {0, 0})).error() == WavError::BadFormat);
  CHECK(parse_wav(wav(1, 1, 192001, 16, {0, 0})).error() == WavError::BadFormat);
  CHECK(parse_wav(wav(1, 1, 4294967291u, 16, {0, 0})).error() == WavError::BadFormat);
  CHECK(parse_wav(wav(1, 1, 192000, 16, {0, 0})).has_value());
  CHECK(parse_wav(wav(2, 1, 44100, 16, {0, 0})).error() == WavError::Unsupported);
  CHECK(parse_wav(wav(1, 1, 44100, 12, {0, 0})).error() == WavError::Unsupported);
  CHECK(parse_wav(wav(1, 1, 44100, 16, {})).error() == WavError::NoData);
}

void truncated_data_is_safe() {
  auto bytes = wav(1, 1, 44100, 16, {0x00, 0x40, 0x00, 0x40, 0x00, 0x40});
  bytes.resize(bytes.size() - 3);  // the data chunk now claims more than exists
  auto p = parse_wav(bytes);
  CHECK(p && p->samples.size() == 1);
}

double rms(std::span<const float> s) {
  double sum = 0;
  for (const float v : s) sum += static_cast<double>(v) * v;
  return std::sqrt(sum / static_cast<double>(s.size()));
}

std::vector<float> sine(double hz, double rate, std::size_t n) {
  std::vector<float> out(n);
  for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<float>(std::sin(2 * std::numbers::pi * hz * static_cast<double>(i) / rate));
  return out;
}

void same_rate_is_a_copy() {
  const std::vector<float> in{0.1f, 0.2f, 0.3f};
  CHECK(Resampler(44100, 44100).run(in) == in);
}

void length_follows_the_ratio() {
  const std::vector<float> in(44100, 0.0f);
  CHECK(Resampler(44100, 48000).run(in).size() == 48000);
  CHECK(Resampler(48000, 44100).run(std::vector<float>(48000)).size() == 44100);
  CHECK(Resampler(44100, 48000).run({}).empty());
}

void a_constant_stays_constant() {
  const std::vector<float> in(4000, 0.5f);
  const auto out = Resampler(44100, 48000).run(in);
  for (std::size_t i = 500; i + 500 < out.size(); ++i) CHECK(near(out[i], 0.5f, 1e-3f));
}

void a_tone_keeps_its_pitch_and_level() {
  const auto in = sine(1000, 44100, 44100);
  const auto out = Resampler(44100, 48000).run(in);
  const auto expected = sine(1000, 48000, out.size());
  // Same phase and amplitude away from the edges.
  double error = 0;
  std::size_t count = 0;
  for (std::size_t i = 1000; i + 1000 < out.size(); ++i, ++count) {
    const double d = out[i] - expected[i];
    error += d * d;
  }
  CHECK(std::sqrt(error / static_cast<double>(count)) < 2e-3);
  CHECK(std::abs(rms(std::span(out).subspan(1000, 40000)) - 0.7071) < 2e-3);
}

void downsampling_removes_what_cannot_fit() {
  const auto out = Resampler(48000, 44100).run(sine(23500, 48000, 48000));  // above 22.05 kHz
  CHECK(rms(std::span(out).subspan(1000, 40000)) < 0.05);
}

}  // namespace

int main() {
  reads_each_sample_format();
  stereo_becomes_mono();
  skips_unknown_chunks_with_padding();
  rejects_bad_files();
  truncated_data_is_safe();
  same_rate_is_a_copy();
  length_follows_the_ratio();
  a_constant_stays_constant();
  a_tone_keeps_its_pitch_and_level();
  downsampling_removes_what_cannot_fit();
  return crpp_test_result();
}
