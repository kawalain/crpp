#pragma once
// Plays Input kinds as sound. Engine owns one worker thread: it opens the device
// when the first sound comes, loads and converts recordings when first needed,
// and stops the device once everything has rung out, so a quiet Room costs
// nothing. play_*() are safe from any thread, take no heap and never wait on the
// disk or the device.
#include "crpp/sound/synth.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "crpp/sound/catalog.hpp"
#include "crpp/sound/mixer.hpp"
#include "crpp/sound/pcm.hpp"
#include "crpp/wire/input_frame.hpp"

namespace crpp::sound {

class Renderer {
 public:
  // On the device's realtime thread: fill interleaved stereo frames.
  virtual void render(std::span<float> stereo) noexcept = 0;

 protected:
  ~Renderer() = default;
};

enum class DeviceError : std::uint8_t { Unavailable, UnsupportedFormat };

// The seam between the engine and the OS. All calls come from one thread.
class AudioDevice {
 public:
  // Returns the rate that render() will be asked to fill.
  [[nodiscard]] virtual std::expected<std::uint32_t, DeviceError> open(Renderer& renderer) = 0;
  [[nodiscard]] virtual bool start() = 0;
  virtual void stop() = 0;
  virtual void close() = 0;
  // The output to use from the next open(): an id the OS gave, or empty for the
  // system's default.
  virtual void select(std::string_view device) = 0;
  // `changed` may run on any thread: the device was lost or replaced, reopen it.
  virtual void set_on_changed(std::function<void()> changed) = 0;

 protected:
  ~AudioDevice() = default;
};

struct Settings {
  int volume = 30;  // percent, of everything
  int keyboard_volume = 100, mouse_volume = 80, pen_volume = 80;
  std::string keyboard_pack, mouse_pack, pen_pack;  // this user's own choice
  bool listen_self = false;
  bool others_use_my_sounds = false;
  bool muted = false;  // the sound of other members only
  bool mute_notifications = false;  // while muted, notification sounds too
  ChimeStyle chimes = ChimeStyle::None;  // a message and someone entering sound like the Skin, not their recordings
  bool idle_muted = false;  // away, and wanting quiet: everything but system sounds
  bool spatial = true;
  int balance = 60;
  Reverb reverb = Reverb::Office;  // apart from `spatial`
  std::string device;  // see AudioDevice::select
};

// A member's chosen packs. Empty means the device's default.
struct Member {
  std::uint32_t slot = 0;
  std::string keyboard_pack, mouse_pack, pen_pack;
  std::string cid;  // stays the same across reconnects; the slot may not
};

// Reads a WAV and converts it to `rate`; empty if unreadable.
[[nodiscard]] std::vector<float> load_recording(const std::filesystem::path& path, std::uint32_t rate);

class Engine final : private Renderer {
 public:
  // `expected`: see Catalog::scan.
  Engine(AudioDevice& device, std::filesystem::path sounds_dir, std::span<const std::string> expected = {});
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  void start();
  void stop();  // idempotent; the device is closed

  void set_settings(Settings settings);
  // What the packs should hold, once a first install has said: see Catalog::scan.
  void set_expected(std::vector<std::string> expected);
  // The Sound packs of the Repositories the user added, after the official ones.
  void set_added(std::vector<PackRoot> roots);
  void set_members(std::vector<Member> members);
  // One member's volume, on top of the others. It outlives set_members, so it holds
  // across reconnects; 100 is what everyone starts with.
  void set_member_volume(std::string cid, int percent);
  void clear_member_volumes();

  void play_own(wire::Kind kind) noexcept;
  void play_peer(std::uint32_t slot, wire::Kind kind) noexcept;

  // By the name of the file in notifications, with or without a leading number.
  // Silent while muted as Settings asks; a preview only needs the volume.
  void play_notification(std::string_view name);
  void preview_notification(std::string_view name);
  // By the name of the file in System. Only needs the volume.
  void play_system(std::string_view name);

  // Opens the device now rather than at the next sound, so that a device the user
  // just picked can be reported as unusable. `failed` runs on the worker thread.
  void check_device();
  void set_on_open_failed(std::function<void()> failed);

 private:
  struct Event {
    std::uint32_t slot = 0;
    wire::Kind kind{};
    bool own = false;
  };
  enum class CueKind : std::uint8_t { Notification, Preview, System };
  struct Cue {
    CueKind kind{};
    std::string name;
  };
  struct VolumeChange {
    std::string cid;  // empty: forget them all
    int percent = 100;
  };
  static constexpr std::size_t max_pending = 128;

  void render(std::span<float> stereo) noexcept override { mixer_.render(stereo); }
  void push(const Event& event) noexcept;
  void run();
  [[nodiscard]] bool ensure_open();
  void close_device();
  void apply_settings();
  void place_members();
  void play(const Event& event);
  void play_cue(const Cue& cue);
  void push_cue(CueKind kind, std::string_view name);
  void start_voice(std::span<const float> samples, const DeskMix& mix, float gain);
  [[nodiscard]] int volume_of(std::uint32_t slot) const;
  [[nodiscard]] std::span<const float> recording(const std::filesystem::path& path);
  [[nodiscard]] std::span<const float> generated(wire::Kind kind, std::string_view pack);
  [[nodiscard]] std::span<const float> generated_cue(const std::string& name);
  [[nodiscard]] std::span<const float> generated_chime(const std::string& name);
  [[nodiscard]] std::string_view pack_for(const Event& event, Device device) const;
  [[nodiscard]] std::size_t desk_of(std::uint32_t slot) const;

  AudioDevice& device_;
  std::filesystem::path sounds_dir_;
  std::vector<std::string> expected_;

  // Shared with the callers; guarded by mutex_.
  std::mutex mutex_;
  std::condition_variable wake_;
  std::array<Event, max_pending> pending_{};
  std::size_t pending_count_ = 0;
  bool stopping_ = false, device_changed_ = false;
  std::optional<Settings> new_settings_;
  std::optional<std::vector<Member>> new_members_;
  std::optional<std::vector<std::string>> new_expected_;
  std::optional<std::vector<PackRoot>> new_added_;
  std::vector<PackRoot> added_;
  std::vector<VolumeChange> new_volumes_;
  std::vector<Cue> new_cues_;
  bool check_device_ = false;
  std::function<void()> on_open_failed_;

  // Worker thread only.
  std::thread worker_;
  Catalog catalog_;
  Random random_;
  Settings settings_;
  std::vector<Member> members_;
  std::vector<std::pair<std::string, int>> member_volumes_;  // cid, percent; 100 is left out
  std::vector<std::pair<std::uint32_t, std::size_t>> desks_;  // slot, desk
  std::array<DeskMix, desk_count> desk_mixes_{};
  std::unordered_map<std::string, std::vector<float>> recordings_;
  std::map<std::uint32_t, Resampler> resamplers_;
  bool open_ = false, running_ = false;
  std::uint32_t rate_ = 0;
  std::chrono::steady_clock::time_point retry_open_at_{};
  Mixer mixer_;
};

}  // namespace crpp::sound
