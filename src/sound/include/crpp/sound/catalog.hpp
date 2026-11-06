#pragma once
// Which recording plays for an Input kind: the Sound packs on disk and the rules
// for choosing among their files. A pack is a folder like keyboard/plastic holding
// pack.json and any of the <sound>[_(hold|release)][_N].wav it has a recording for.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "crpp/wire/input_frame.hpp"

namespace crpp::sound {

enum class Device : std::uint8_t { Keyboard, Mouse, Pen };

[[nodiscard]] std::optional<std::string_view> sound_name(wire::Kind kind) noexcept;
[[nodiscard]] Device device_of(wire::Kind kind) noexcept;

// A small fast generator; recordings are chosen at random, not securely.
class Random {
 public:
  explicit Random(std::uint64_t seed) noexcept : state_(seed == 0 ? 0x9E3779B97F4A7C15ull : seed) {}
  [[nodiscard]] std::uint64_t next() noexcept {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 7;
    state_ ^= state_ << 17;
    return state_;
  }
  [[nodiscard]] std::size_t below(std::size_t n) noexcept { return n <= 1 ? 0 : static_cast<std::size_t>(next() % n); }

 private:
  std::uint64_t state_;
};

// A notification sound, by its file's name, or by that name with the number upstream
// puts in front: "cloud_chat" and "01_cloud_chat" both find notifications/cloud_chat.wav.
[[nodiscard]] std::optional<std::filesystem::path> notification_file(const std::filesystem::path& root, std::string_view name);

struct PackInfo {
  std::string id, name;
};

class Catalog {
 public:
  // Reads the folder structure under `root` (the "sounds" folder). `expected` lists files
  // relative to it that belong there, on disk or not, like "keyboard/plastic/left.wav":
  // a pack still downloading can be chosen, and a sound it deliberately lacks stays silent.
  [[nodiscard]] static Catalog scan(const std::filesystem::path& root, std::span<const std::string> expected = {});

  [[nodiscard]] bool empty() const noexcept { return packs_.empty(); }
  // The pack ids of a device, in display order. The first is its default.
  [[nodiscard]] std::vector<std::string> packs(Device device) const;
  // What a user can pick for a device: only packs with recordings, by their name.
  [[nodiscard]] std::vector<PackInfo> choices(Device device) const;

  // The file to play, or empty if this pack has no sound for the kind. An unknown
  // pack means the device's default.
  [[nodiscard]] std::optional<std::filesystem::path> pick(wire::Kind kind, std::string_view pack, Random& random) const;

 private:
  struct Pack {
    Device device;
    std::string id;
    std::string name;
    std::filesystem::path dir;
    std::vector<std::string> files;  // as on disk; compared without case
  };
  [[nodiscard]] const Pack* find(Device device, std::string_view id) const noexcept;
  std::vector<Pack> packs_;
};

}  // namespace crpp::sound
