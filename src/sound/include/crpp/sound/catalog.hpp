#pragma once
// Which recording plays for an Input kind: the Sound packs and the rules for choosing
// among their files. A pack is a folder like keyboard/plastic holding pack.json and any of
// the <sound>[_(hold|release)][_N].wav it has a recording for. Folders are those of a
// files::Tree, whose files may be anywhere on disk (ADR 0010).
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "crpp/files/tree.hpp"
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
[[nodiscard]] std::optional<std::filesystem::path> notification_file(const files::Tree& sounds, std::string_view name);

struct PackInfo {
  std::string id, name;
};

// The Sound packs of the official Repository, or of another, whose pack ids get its prefix and a
// slash: a tree of the "sounds" folder, as "keyboard/plastic/left.wav". It may list files that
// are not there yet: a pack still downloading can be chosen, and a sound it deliberately lacks
// stays silent.
struct PackRoot {
  files::Tree tree;
  std::string prefix;
};

class Catalog {
 public:
  // The packs of a tree of the "sounds" folder: each folder under keyboard, mouse or pen with a
  // pack.json.
  [[nodiscard]] static Catalog scan(const files::Tree& sounds, std::string_view language = "ko");
  // Several folders, each device's packs in their order: the first folder's first pack is the
  // device's default, which another member's pack this client lacks plays as.
  [[nodiscard]] static Catalog scan(std::span<const PackRoot> roots, std::string_view language = "ko");

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
    files::Tree tree;                // its own folder's
    std::vector<std::string> files;  // its file names
  };
  [[nodiscard]] const Pack* find(Device device, std::string_view id) const noexcept;
  std::vector<Pack> packs_;
};

}  // namespace crpp::sound
