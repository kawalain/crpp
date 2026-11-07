#pragma once
// Sounds made from numbers, for when a recording could not be fetched. They stand in
// and do not imitate: a short burst of filtered noise and one or two decaying tones,
// brighter or longer per pack, from what the recordings of that pack measure. Pure.
#include <cstdint>
#include <string_view>
#include <vector>

#include "crpp/wire/input_frame.hpp"

namespace crpp::sound {

inline constexpr std::uint32_t synth_variants = 3;
inline constexpr float synth_longest_ms = 80;

// One of a few variations of the kind's sound, at most synth_longest_ms long. An unknown
// pack sounds like the device's default.
[[nodiscard]] std::vector<float> synthesize(wire::Kind kind, std::string_view pack, std::uint32_t rate, std::uint32_t variant);

// Whether a notification or system sound of that file name has a generated stand-in.
[[nodiscard]] bool can_synthesize_cue(std::string_view name) noexcept;
// A short rise or fall of two tones for one; empty for any other name.
[[nodiscard]] std::vector<float> synthesize_cue(std::string_view name, std::uint32_t rate);

// The chimes of a Skin that has its own notification sounds: small bells going up, made here and
// not modelled on any recording. Only a message and someone entering have one.
inline constexpr float chime_longest_ms = 500;
[[nodiscard]] bool can_chime(std::string_view name) noexcept;
// Empty for any other name.
[[nodiscard]] std::vector<float> synthesize_chime(std::string_view name, std::uint32_t rate);

}  // namespace crpp::sound
