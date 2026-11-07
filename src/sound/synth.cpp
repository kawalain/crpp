#include "crpp/sound/synth.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>

#include "crpp/sound/catalog.hpp"

namespace crpp::sound {

namespace {

// Measured from each pack's plain presses: the spectral centroid, the time to fall 30 dB,
// and the peak level. Only the character of a pack survives this.
struct PackSound {
  std::string_view id;
  Device device;
  float bright_hz, fall_ms, level;
};

constexpr PackSound packs[] = {
    {"plastic", Device::Keyboard, 4300, 101, 0.61f},
    {"thocky", Device::Keyboard, 4000, 79, 0.88f},
    {"walnut", Device::Keyboard, 4865, 127, 0.79f},
    {"plastic", Device::Mouse, 6181, 148, 0.90f},
    {"rubber", Device::Mouse, 2395, 198, 0.14f},
    {"walnut", Device::Mouse, 9085, 125, 0.70f},
    {"paper", Device::Pen, 2217, 85, 0.08f},
};

const PackSound& pack_sound(Device device, std::string_view id) {
  const PackSound* first = nullptr;
  for (const PackSound& pack : packs) {
    if (pack.device != device) continue;
    if (!first) first = &pack;
    if (pack.id == id) return pack;
  }
  return *first;
}

// What separates the sounds of one device.
struct Shape {
  float bright = 1, fall = 1, level = 1;
  float body_hz = 180;   // the case or the shell ringing under the click
  float body = 0.35f;    // its share of the sound
};

Shape shape_of(wire::Kind kind) {
  const std::string_view name = sound_name(kind).value_or(std::string_view{});
  const auto has = [&](std::string_view part) { return name.find(part) != std::string_view::npos; };
  Shape shape;
  if (has("space")) {
    shape.body_hz = 130;
    shape.body = 0.5f;
    shape.fall = 1.15f;
  } else if (name.starts_with("mod_")) {
    shape.bright = 0.8f;
    shape.body_hz = 150;
    shape.body = 0.4f;
  } else if (name == "wheel") {
    shape.bright = 0.6f;
    shape.fall = 0.5f;
    shape.level = 0.2f;
    shape.body = 0.1f;
  } else if (name == "pen_stroke") {
    shape.bright = 2.3f;
    shape.level = 0.3f;
    shape.body = 0;
  } else if (device_of(kind) == Device::Mouse) {
    shape.body_hz = 700;
    shape.body = 0.2f;
  }
  if (has("hold")) {
    shape.fall *= 0.5f;
    shape.level *= 0.8f;
  } else if (has("release")) {
    shape.fall *= 0.5f;
    shape.level *= 0.5f;
    shape.body *= 0.5f;
  }
  return shape;
}

std::uint32_t mix_seed(std::uint32_t seed, std::string_view text) {
  for (const char c : text) seed = (seed ^ static_cast<std::uint8_t>(c)) * 16777619u;
  return seed;
}

}  // namespace

std::vector<float> synthesize(wire::Kind kind, std::string_view pack, std::uint32_t rate, std::uint32_t variant) {
  if (rate == 0) return {};
  const PackSound& sound = pack_sound(device_of(kind), pack);
  const Shape shape = shape_of(kind);

  // Each variation is a little brighter or darker, as no two presses are alike.
  std::uint32_t seed = mix_seed(2166136261u + variant, sound.id);
  seed = mix_seed(seed, sound_name(kind).value_or(std::string_view{}));
  const float nudge = 1 + 0.08f * (static_cast<float>(variant % synth_variants) - 1);

  const float sample_rate = static_cast<float>(rate);
  const auto count = static_cast<std::size_t>(sample_rate * synth_longest_ms / 1000);
  // 30 dB is ln(31.6) = 3.45 time constants. Long recordings still end inside the limit.
  const float tau = std::min(sound.fall_ms * shape.fall, synth_longest_ms) / 1000 / 3.45f;
  const float noise_decay = std::exp(-1 / (tau * 0.4f * sample_rate));
  const float body_decay = std::exp(-1 / (tau * sample_rate));
  // A one-pole low-pass at the pack's brightness, minus a slower one: a band of noise.
  const float hz = std::min(sound.bright_hz * shape.bright * nudge, sample_rate * 0.45f);
  const float high = 1 - std::exp(-2 * std::numbers::pi_v<float> * hz / sample_rate);
  const float low = 1 - std::exp(-2 * std::numbers::pi_v<float> * (hz * 0.25f) / sample_rate);
  const float step = 2 * std::numbers::pi_v<float> * shape.body_hz * nudge / sample_rate;

  std::vector<float> out(count);
  float noise_env = 1, body_env = 1, a = 0, b = 0, peak = 0;
  for (std::size_t i = 0; i < count; ++i) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    const float white = static_cast<float>(seed) / 2147483648.0f - 1;
    a += high * (white - a);
    b += low * (white - b);
    const float value = (a - b) * noise_env * (1 - shape.body) +
                        std::sin(step * static_cast<float>(i)) * body_env * shape.body;
    out[i] = value;
    peak = std::max(peak, std::abs(value));
    noise_env *= noise_decay;
    body_env *= body_decay;
  }
  const float gain = peak > 0 ? sound.level * shape.level / peak : 0;
  for (std::size_t i = 0; i < count; ++i) {
    // The tail fades to nothing, so stopping at the limit does not click.
    const float left = static_cast<float>(count - i) / static_cast<float>(count);
    out[i] *= gain * std::min(1.0f, left * 8);
  }
  return out;
}

bool can_synthesize_cue(std::string_view name) noexcept {
  return name.ends_with("cloud_chat") || name.ends_with("cloud_enter") || name == "mute" || name == "unmute";
}

std::vector<float> synthesize_cue(std::string_view name, std::uint32_t rate) {
  if (rate == 0 || !can_synthesize_cue(name)) return {};
  float first = 523, second = 784, each_ms = 70;
  if (name.ends_with("cloud_enter")) {
    first = 392;
    second = 587;
    each_ms = 100;
  } else if (name == "mute") {
    first = 660;
    second = 440;
    each_ms = 45;
  } else if (name == "unmute") {
    first = 440;
    second = 660;
    each_ms = 45;
  }
  const float sample_rate = static_cast<float>(rate);
  const auto each = static_cast<std::size_t>(sample_rate * each_ms / 1000);
  std::vector<float> out;
  out.reserve(each * 2);
  for (const float hz : {first, second}) {
    const float step = 2 * std::numbers::pi_v<float> * hz / sample_rate;
    for (std::size_t i = 0; i < each; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(each);
      const float attack = std::min(1.0f, t * 20);
      out.push_back(0.4f * std::sin(step * static_cast<float>(i)) * attack * (1 - t) * (1 - t));
    }
  }
  return out;
}

bool can_chime(std::string_view name) noexcept { return name.ends_with("cloud_chat") || name.ends_with("cloud_enter"); }

std::vector<float> synthesize_chime(std::string_view name, std::uint32_t rate, ChimeStyle style) {
  if (rate == 0 || !can_chime(name) || style == ChimeStyle::None) return {};
  struct Note {
    float at_ms, hz;
  };
  // Two notes for a message, three quicker ones for someone entering.
  constexpr Note message[] = {{0, 1319}, {105, 1760}};
  constexpr Note enter[] = {{0, 1047}, {75, 1319}, {150, 1568}};
  constexpr Note plain_message[] = {{0, 660}};
  constexpr Note plain_enter[] = {{0, 523}, {120, 660}};
  const bool plain = style == ChimeStyle::Plain, is_message = name.ends_with("cloud_chat");
  const std::span<const Note> notes = plain ? (is_message ? std::span<const Note>(plain_message) : std::span<const Note>(plain_enter))
                                            : (is_message ? std::span<const Note>(message) : std::span<const Note>(enter));

  const float sample_rate = static_cast<float>(rate);
  const auto count = static_cast<std::size_t>(sample_rate * chime_longest_ms / 1000);
  std::vector<float> out(count);
  constexpr float two_pi = 2 * std::numbers::pi_v<float>;
  for (const Note& note : notes) {
    const auto start = static_cast<std::size_t>(sample_rate * note.at_ms / 1000);
    const float tau = (plain ? 0.11f : 0.075f) * sample_rate;
    // A bell's overtone is not a whole multiple of its note, and it dies sooner. A plain tone's is.
    const float step = two_pi * note.hz / sample_rate, overtone = step * (plain ? 2.0f : 2.76f);
    for (std::size_t i = start; i < count; ++i) {
      const auto n = static_cast<float>(i - start);
      const float attack = std::min(1.0f, n / (0.002f * sample_rate));
      out[i] += attack * (std::sin(step * n) * std::exp(-n / tau) + 0.3f * std::sin(overtone * n) * std::exp(-n / (tau * 0.35f)));
    }
  }
  float peak = 0;
  for (const float v : out) peak = std::max(peak, std::abs(v));
  const float gain = peak > 0 ? 0.45f / peak : 0;
  for (std::size_t i = 0; i < count; ++i) {
    const float left = static_cast<float>(count - i) / static_cast<float>(count);
    out[i] *= gain * std::min(1.0f, left * 6);
  }
  return out;
}

}  // namespace crpp::sound
