#include "crpp/sound/engine.hpp"

#include <atomic>
#include <mutex>
#include <thread>

#include "check.hpp"

using namespace crpp::sound;
using crpp::wire::Kind;
using namespace std::chrono_literals;

namespace {

// Pulled by the test thread instead of a realtime one.
class FakeDevice final : public AudioDevice {
 public:
  std::atomic<std::uint32_t> rate{48000};
  std::atomic<bool> fail_open{false}, fail_start{false};
  std::atomic<int> opens{0}, closes{0}, starts{0}, stops{0};

  std::expected<std::uint32_t, DeviceError> open(Renderer& r) override {
    ++opens;
    if (fail_open) return std::unexpected(DeviceError::Unavailable);
    std::lock_guard lock(mutex_);
    renderer_ = &r;
    return rate.load();
  }
  bool start() override {
    if (fail_start) return false;
    ++starts;
    running_ = true;
    return true;
  }
  void stop() override {
    ++stops;
    running_ = false;
  }
  void close() override {
    ++closes;
    std::lock_guard lock(mutex_);
    renderer_ = nullptr;
  }
  void set_on_changed(std::function<void()> changed) override {
    std::lock_guard lock(mutex_);
    changed_ = std::move(changed);
  }
  void change() {
    std::function<void()> f;
    {
      std::lock_guard lock(mutex_);
      f = changed_;
    }
    if (f) f();
  }
  // The loudest value of the next frames; 0 while not running.
  float pump(std::size_t frames = 2048) {
    std::lock_guard lock(mutex_);
    if (!running_ || !renderer_) return 0;
    std::vector<float> out(frames * 2);
    renderer_->render(out);
    float peak = 0;
    for (const float v : out) peak = std::max(peak, std::abs(v));
    return peak;
  }

 private:
  std::mutex mutex_;
  Renderer* renderer_ = nullptr;
  std::function<void()> changed_;
  std::atomic<bool> running_{false};
};

// Keeps rendering until the condition holds, as a real device would.
template <typename F>
bool eventually(FakeDevice& device, F condition, std::chrono::milliseconds limit = 5s) {
  const auto end = std::chrono::steady_clock::now() + limit;
  while (std::chrono::steady_clock::now() < end) {
    if (condition()) return true;
    (void)device.pump(1024);
    std::this_thread::sleep_for(2ms);
  }
  return condition();
}

float loudest(FakeDevice& device, std::chrono::milliseconds time = 200ms) {
  const auto end = std::chrono::steady_clock::now() + time;
  float peak = 0;
  while (std::chrono::steady_clock::now() < end) {
    peak = std::max(peak, device.pump(1024));
    std::this_thread::sleep_for(1ms);
  }
  return peak;
}

Settings full_volume() {
  Settings s;
  s.volume = 100;
  return s;
}

void nothing_happens_until_a_sound() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  engine.start();
  std::this_thread::sleep_for(50ms);
  CHECK(device.opens == 0);
  engine.stop();
  CHECK(device.opens == 0);
}

void a_peer_sounds_and_the_stream_stops_afterwards() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  engine.set_settings(full_volume());
  engine.set_members({{3, "walnut", "walnut", ""}});
  engine.start();
  engine.play_peer(3, Kind::KeySpace);
  CHECK(eventually(device, [&] { return device.starts.load() == 1; }));
  CHECK(loudest(device) > 0.001f);
  CHECK(eventually(device, [&] { return device.stops.load() == 1; }, 15s));
  CHECK(device.opens == 1);
  // The next sound uses the open device again.
  engine.play_peer(3, Kind::KeySpace);
  CHECK(eventually(device, [&] { return device.starts.load() == 2; }));
  CHECK(device.opens == 1);
  engine.stop();
  CHECK(device.closes == 1);
}

void own_sounds_need_listen_self() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  engine.set_settings(full_volume());
  engine.start();
  engine.play_own(Kind::KeySpace);
  std::this_thread::sleep_for(100ms);
  CHECK(device.starts == 0);
  Settings settings = full_volume();
  settings.listen_self = true;
  engine.set_settings(settings);
  engine.play_own(Kind::KeySpace);
  CHECK(eventually(device, [&] { return device.starts.load() == 1; }));
  engine.stop();
}

void muting_silences_only_others() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  Settings settings = full_volume();
  settings.muted = true;
  settings.listen_self = true;
  engine.set_settings(settings);
  engine.start();
  engine.play_peer(1, Kind::KeySpace);
  std::this_thread::sleep_for(100ms);
  CHECK(device.starts == 0);
  engine.play_own(Kind::KeySpace);
  CHECK(eventually(device, [&] { return device.starts.load() == 1; }));
  engine.stop();
}

void zero_volume_keeps_the_device_closed() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  Settings settings;
  settings.volume = 0;
  engine.set_settings(settings);
  engine.start();
  engine.play_peer(1, Kind::KeySpace);
  std::this_thread::sleep_for(100ms);
  CHECK(device.opens == 0);
  settings = full_volume();
  settings.keyboard_volume = 0;
  engine.set_settings(settings);
  engine.play_peer(1, Kind::KeySpace);
  std::this_thread::sleep_for(100ms);
  CHECK(device.opens == 0);
  engine.stop();
}

void kinds_without_a_sound_are_silent() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  engine.set_settings(full_volume());
  engine.start();
  engine.play_peer(1, Kind::KeyNormal);
  engine.play_peer(1, static_cast<Kind>(200));
  std::this_thread::sleep_for(100ms);
  CHECK(device.opens == 0);
  engine.stop();
}

void a_failed_open_is_retried_later_not_for_every_key() {
  FakeDevice device;
  device.fail_open = true;
  Engine engine(device, CRPP_SOUNDS_DIR);
  engine.set_settings(full_volume());
  engine.start();
  for (int i = 0; i < 20; ++i) {
    engine.play_peer(1, Kind::KeySpace);
    std::this_thread::sleep_for(2ms);
  }
  std::this_thread::sleep_for(50ms);
  CHECK(device.opens == 1);
  CHECK(device.starts == 0);
  engine.stop();
}

void a_failed_start_closes_the_device() {
  FakeDevice device;
  device.fail_start = true;
  Engine engine(device, CRPP_SOUNDS_DIR);
  engine.set_settings(full_volume());
  engine.start();
  engine.play_peer(1, Kind::KeySpace);
  CHECK(eventually(device, [&] { return device.closes.load() == 1; }));
  engine.stop();
  CHECK(device.closes == 1);
}

void a_changed_device_is_reopened_at_its_rate() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  engine.set_settings(full_volume());
  engine.start();
  engine.play_peer(1, Kind::KeySpace);
  CHECK(eventually(device, [&] { return device.starts.load() == 1; }));
  device.rate = 44100;
  device.change();
  CHECK(eventually(device, [&] { return device.closes.load() == 1; }));
  CHECK(device.opens == 1);  // not until something plays
  engine.play_peer(1, Kind::KeySpace);
  CHECK(eventually(device, [&] { return device.opens.load() == 2 && device.starts.load() == 2; }));
  CHECK(loudest(device) > 0.001f);
  engine.stop();
}

void a_bad_folder_is_silent() {
  FakeDevice device;
  Engine engine(device, std::filesystem::path(CRPP_SOUNDS_DIR) / "none");
  engine.set_settings(full_volume());
  engine.start();
  engine.play_peer(1, Kind::KeySpace);
  std::this_thread::sleep_for(100ms);
  CHECK(device.opens == 0);
  engine.stop();
}

void a_burst_never_blocks_or_fails() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  engine.set_settings(full_volume());
  engine.set_members({{1, "", "", ""}, {2, "thocky", "", ""}});
  engine.start();
  for (int i = 0; i < 5000; ++i) engine.play_peer(1 + static_cast<std::uint32_t>(i % 2), Kind::KeyAny);
  CHECK(eventually(device, [&] { return device.starts.load() >= 1; }));
  (void)loudest(device, 100ms);
  engine.stop();
}

void restarts_cleanly() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  engine.start();
  engine.start();
  engine.stop();
  engine.stop();
  engine.set_settings(full_volume());
  engine.start();
  engine.play_peer(1, Kind::KeySpace);
  CHECK(eventually(device, [&] { return device.starts.load() == 1; }));
  engine.stop();
}

}  // namespace

int main() {
  nothing_happens_until_a_sound();
  a_peer_sounds_and_the_stream_stops_afterwards();
  own_sounds_need_listen_self();
  muting_silences_only_others();
  zero_volume_keeps_the_device_closed();
  kinds_without_a_sound_are_silent();
  a_failed_open_is_retried_later_not_for_every_key();
  a_failed_start_closes_the_device();
  a_changed_device_is_reopened_at_its_rate();
  a_bad_folder_is_silent();
  a_burst_never_blocks_or_fails();
  restarts_cleanly();
  return crpp_test_result();
}
