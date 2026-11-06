// crpp_soundtest: hear the spatial mix, to tune it by ear.
//   crpp_soundtest <sounds folder> [--pack plastic] [--balance 60] [--flat] [--synth]
//                  [--volume 30] [--repeat 3]
// Walks through every seat of the virtual office, playing a few key presses from
// each and printing where they should seem to come from.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "crpp/sound/catalog.hpp"
#include "crpp/sound/engine.hpp"
#include "crpp/sound/mixer.hpp"
#include "crpp/sound/synth.hpp"
#include "crpp/sound/wasapi_device.hpp"

using namespace crpp;
using namespace std::chrono_literals;

namespace {

class MixerRenderer final : public sound::Renderer {
 public:
  explicit MixerRenderer(sound::Mixer& mixer) : mixer_(mixer) {}
  void render(std::span<float> stereo) noexcept override { mixer_.render(stereo); }

 private:
  sound::Mixer& mixer_;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: crpp_soundtest <sounds folder> [--pack id] [--balance n] [--flat] [--synth] [--volume n] [--repeat n]\n");
    return 2;
  }
  const std::filesystem::path sounds = argv[1];
  std::string pack;
  bool synth = false;
  sound::SpatialConfig config;
  int volume = 30, repeat = 3;
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--flat") config.spatial = false;
    else if (arg == "--synth") synth = true;
    else if (arg == "--pack" && has_value) pack = argv[++i];
    else if (arg == "--balance" && has_value) config.balance = std::atoi(argv[++i]);
    else if (arg == "--volume" && has_value) volume = std::atoi(argv[++i]);
    else if (arg == "--repeat" && has_value) repeat = std::atoi(argv[++i]);
  }

  const auto catalog = sound::Catalog::scan(sounds);
  if (catalog.empty()) {
    std::fprintf(stderr, "no sound packs in %s\n", sounds.string().c_str());
    return 1;
  }
  auto mixer = std::make_unique<sound::Mixer>();
  MixerRenderer renderer(*mixer);
  sound::WasapiDevice device;
  const auto rate = device.open(renderer);
  if (!rate) {
    std::fprintf(stderr, "no usable output device\n");
    return 1;
  }
  mixer->prepare(*rate);
  const float v = static_cast<float>(std::clamp(volume, 0, 100)) / 100.0f;
  mixer->set_master(v * v);
  std::printf("output: %u Hz, pack: %s, spatial: %s, balance: %d, volume: %d\n", *rate, pack.empty() ? "(default)" : pack.c_str(),
              config.spatial ? "on" : "off", config.balance, volume);
  if (!device.start()) {
    std::fprintf(stderr, "could not start the stream\n");
    return 1;
  }

  sound::Random random(std::random_device{}());
  std::vector<std::vector<float>> kept;
  kept.reserve(256);  // voices point into these; they must not move
  const auto play = [&](wire::Kind kind, const sound::SeatMix& mix) {
    const auto file = catalog.pick(kind, pack, random);
    if (!file || kept.size() == kept.capacity()) return;
    kept.push_back(synth ? sound::synthesize(kind, pack, *rate, static_cast<std::uint32_t>(random.below(sound::synth_variants)))
                         : sound::load_recording(*file, *rate));
    (void)mixer->trigger(kept.back(), mix, 1.0f);
  };

  std::printf("your own sound, in the middle of your head\n");
  for (int i = 0; i < repeat; ++i) {
    play(wire::Kind::KeyAny, sound::own_mix());
    std::this_thread::sleep_for(220ms);
  }
  std::this_thread::sleep_for(500ms);
  for (std::size_t seat = 0; seat < sound::seat_count; ++seat) {
    const std::size_t column = seat % 4, row = seat / 4;
    std::printf("seat %2zu  %s %s\n", seat, row < 2 ? "behind  " : "in front",
                column == 0 ? "far left" : column == 1 ? "left" : column == 2 ? "right" : "far right");
    std::fflush(stdout);
    const auto mix = sound::seat_mix(seat, *rate, config);
    for (int i = 0; i < repeat; ++i) {
      play(wire::Kind::KeyAny, mix);
      std::this_thread::sleep_for(220ms);
    }
    std::this_thread::sleep_for(500ms);
  }
  std::this_thread::sleep_for(1s);
  device.close();
  return 0;
}
