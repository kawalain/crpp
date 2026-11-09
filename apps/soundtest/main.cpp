// crpp_soundtest: hear the spatial mix, to tune it by ear.
//   crpp_soundtest --chimes [--volume 30] [--repeat 3]
//   crpp_soundtest (<sounds folder> | --synth) [--pack plastic] [--balance 60] [--flat]
//                  [--reverb off|office|large] [--volume 30] [--repeat 3]
// Walks through every desk of the virtual office, playing a few key presses from
// each and printing where they should seem to come from. A desk in front and the one
// behind it sound the same: only left to right and near to far can be told.
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
  std::filesystem::path sounds;
  std::string pack;
  bool synth = false, chimes = false;
  sound::SpatialConfig config;
  int volume = 30, repeat = 3;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--flat") config.spatial = false;
    else if (arg == "--synth") synth = true;
    else if (arg == "--chimes") chimes = true;
    else if (arg == "--pack" && has_value) pack = argv[++i];
    else if (arg == "--balance" && has_value) config.balance = std::atoi(argv[++i]);
    else if (arg == "--reverb" && has_value) {
      const std::string name = argv[++i];
      config.reverb = name == "off" ? sound::Reverb::Off : name == "large" ? sound::Reverb::Large : sound::Reverb::Office;
    }
    else if (arg == "--volume" && has_value) volume = std::atoi(argv[++i]);
    else if (arg == "--repeat" && has_value) repeat = std::atoi(argv[++i]);
    else if (!arg.starts_with("--")) sounds = arg;
  }
  if (sounds.empty() && !synth && !chimes) {
    std::fprintf(stderr, "usage: crpp_soundtest --chimes | (<sounds folder> | --synth) [--pack id] [--balance n] [--flat] [--reverb off|office|large] [--volume n] [--repeat n]\n");
    return 2;
  }

  // Generated sounds need no files.
  const auto catalog = synth || chimes ? sound::Catalog{} : sound::Catalog::scan(files::Tree::of_folder(sounds));
  if (!synth && !chimes && catalog.empty()) {
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
  mixer->set_reverb(config.reverb);
  const char* reverb_name = config.reverb == sound::Reverb::Off ? "off" : config.reverb == sound::Reverb::Large ? "large" : "office";
  std::printf("output: %u Hz, pack: %s, spatial: %s, balance: %d, reverb: %s, volume: %d\n", *rate, pack.empty() ? "(default)" : pack.c_str(),
              config.spatial ? "on" : "off", config.balance, reverb_name, volume);
  if (!device.start()) {
    std::fprintf(stderr, "could not start the stream\n");
    return 1;
  }

  sound::Random random(std::random_device{}());
  std::vector<std::vector<float>> kept;
  kept.reserve(256);  // voices point into these; they must not move
  const auto play = [&](wire::Kind kind, const sound::DeskMix& mix) {
    if (kept.size() == kept.capacity()) return;
    if (synth) {
      kept.push_back(sound::synthesize(kind, pack, *rate, static_cast<std::uint32_t>(random.below(sound::synth_variants))));
    } else {
      const auto file = catalog.pick(kind, pack, random);
      if (!file) return;
      kept.push_back(sound::load_recording(*file, *rate));
    }
    (void)mixer->trigger(kept.back(), mix, 1.0f);
  };

  if (chimes) {
    for (const sound::ChimeStyle style : {sound::ChimeStyle::Bells, sound::ChimeStyle::Plain}) {
      for (const char* name : {"cloud_chat", "cloud_enter"}) {
        std::printf("%s %s\n", style == sound::ChimeStyle::Bells ? "bells" : "plain", name);
        std::fflush(stdout);
        kept.push_back(sound::synthesize_chime(name, *rate, style));
        for (int i = 0; i < repeat; ++i) {
          (void)mixer->trigger(kept.back(), sound::own_mix(), 1.0f);
          std::this_thread::sleep_for(1500ms);
        }
      }
    }
    device.close();
    return 0;
  }

  std::printf("your own sound, in the middle of your head\n");
  for (int i = 0; i < repeat; ++i) {
    play(wire::Kind::KeyAny, sound::own_mix());
    std::this_thread::sleep_for(220ms);
  }
  std::this_thread::sleep_for(500ms);
  for (std::size_t desk = 0; desk < sound::desk_count; ++desk) {
    const std::size_t column = desk % 4, row = desk / 4;
    std::printf("desk %2zu  %s %s\n", desk, row == 0 || row == 3 ? "far row " : "near row",
                column == 0 ? "far left" : column == 1 ? "left" : column == 2 ? "right" : "far right");
    std::fflush(stdout);
    const auto mix = sound::desk_mix(desk, *rate, config);
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
