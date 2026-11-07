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
  std::string selected() {
    std::lock_guard lock(mutex_);
    return selected_;
  }

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
  void select(std::string_view device) override {
    std::lock_guard lock(mutex_);
    selected_ = std::string(device);
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
  std::string selected_;
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
  engine.set_members({{3, "walnut", "walnut", "", ""}});
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
  engine.set_members({{1, "", "", "", ""}, {2, "thocky", "", "", ""}});
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

void a_member_at_zero_is_silent_and_stays_so_across_reconnects() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  engine.set_settings(full_volume());
  engine.set_members({{1, "", "", "", "cid-a"}, {2, "", "", "", "cid-b"}});
  engine.set_member_volume("cid-a", 0);
  engine.start();
  engine.play_peer(1, Kind::KeySpace);
  std::this_thread::sleep_for(100ms);
  CHECK(device.opens == 0);
  // Back with another slot, as after a reconnect.
  engine.set_members({{5, "", "", "", "cid-a"}});
  engine.play_peer(5, Kind::KeySpace);
  std::this_thread::sleep_for(100ms);
  CHECK(device.opens == 0);
  engine.set_member_volume("cid-a", 100);
  engine.play_peer(5, Kind::KeySpace);
  CHECK(eventually(device, [&] { return device.starts.load() == 1; }));
  engine.stop();
}

void a_member_volume_scales_only_that_member() {
  const auto peak_at = [](int percent) {
    FakeDevice device;
    Engine engine(device, CRPP_SOUNDS_DIR);
    Settings settings = full_volume();
    settings.spatial = false;
    engine.set_settings(settings);
    engine.set_members({{1, "plastic", "", "", "cid-a"}});
    engine.set_member_volume("cid-a", percent);
    engine.set_member_volume("someone-else", 0);
    engine.start();
    engine.play_peer(1, Kind::KeySpace);
    (void)eventually(device, [&] { return device.starts.load() == 1; });
    const float peak = loudest(device);
    engine.stop();
    return peak;
  };
  const float full = peak_at(100), half = peak_at(50);
  CHECK(full > 0.001f);
  // The same curve as every other volume: a quarter of the amplitude.
  CHECK(half > full * 0.2f && half < full * 0.3f);
}

void clearing_member_volumes_forgets_them() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  engine.set_settings(full_volume());
  engine.set_members({{1, "", "", "", "cid-a"}});
  engine.set_member_volume("cid-a", 0);
  engine.clear_member_volumes();
  engine.start();
  engine.play_peer(1, Kind::KeySpace);
  CHECK(eventually(device, [&] { return device.starts.load() == 1; }));
  engine.stop();
}

void an_expected_file_that_is_absent_plays_a_generated_sound() {
  FakeDevice device;
  const std::vector<std::string> expected{"keyboard/plastic/mod_space.wav"};
  Engine engine(device, std::filesystem::path(CRPP_SOUNDS_DIR) / "none", expected);
  Settings settings = full_volume();
  settings.listen_self = true;
  engine.set_settings(settings);
  engine.start();
  engine.play_own(Kind::KeySpace);
  CHECK(eventually(device, [&] { return device.starts.load() == 1; }));
  CHECK(loudest(device) > 0.001f);
  engine.stop();
}

void a_sound_the_pack_does_not_have_stays_silent() {
  FakeDevice device;
  const std::vector<std::string> expected{"keyboard/plastic/mod_space.wav"};
  Engine engine(device, std::filesystem::path(CRPP_SOUNDS_DIR) / "none", expected);
  Settings settings = full_volume();
  settings.listen_self = true;
  engine.set_settings(settings);
  engine.start();
  engine.play_own(Kind::KeyEnter);
  std::this_thread::sleep_for(100ms);
  CHECK(device.starts.load() == 0);
  engine.stop();
}

void an_absent_notification_plays_a_generated_cue() {
  FakeDevice device;
  Engine engine(device, std::filesystem::path(CRPP_SOUNDS_DIR) / "none");
  engine.set_settings(full_volume());
  engine.start();
  engine.play_notification("cloud_chat");
  CHECK(eventually(device, [&] { return device.starts.load() == 1; }));
  CHECK(loudest(device) > 0.001f);
  engine.stop();
}

void chimes_take_the_place_of_a_skins_notifications() {
  FakeDevice device;
  Engine engine(device, std::filesystem::path(CRPP_SOUNDS_DIR) / "none");
  Settings settings = full_volume();
  settings.chimes = ChimeStyle::Bells;
  engine.set_settings(settings);
  engine.start();
  engine.play_notification("no_such_sound");  // nothing to play and no chime for it
  std::this_thread::sleep_for(100ms);
  CHECK(device.starts.load() == 0);
  engine.preview_notification("cloud_chat");
  CHECK(eventually(device, [&] { return device.starts.load() == 1; }));
  CHECK(loudest(device) > 0.001f);
  engine.stop();
}

void notifications_follow_the_mute_settings() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  Settings settings = full_volume();
  settings.muted = true;
  settings.mute_notifications = true;
  engine.set_settings(settings);
  engine.start();
  engine.play_notification("01_cloud_chat");
  std::this_thread::sleep_for(100ms);
  CHECK(device.opens == 0);
  settings = full_volume();
  settings.idle_muted = true;
  engine.set_settings(settings);
  engine.play_notification("cloud_chat");
  std::this_thread::sleep_for(100ms);
  CHECK(device.opens == 0);
  // Muted without the setting: notifications go on.
  settings = full_volume();
  settings.muted = true;
  engine.set_settings(settings);
  engine.play_notification("cloud_enter");
  CHECK(eventually(device, [&] { return device.starts.load() == 1; }));
  CHECK(loudest(device) > 0.001f);
  engine.stop();
}

void previews_and_system_sounds_need_only_the_volume() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  Settings settings = full_volume();
  settings.muted = true;
  settings.mute_notifications = true;
  settings.idle_muted = true;
  engine.set_settings(settings);
  engine.start();
  engine.preview_notification("cloud_chat");
  CHECK(eventually(device, [&] { return device.starts.load() == 1; }));
  CHECK(eventually(device, [&] { return device.stops.load() == 1; }, 15s));
  engine.play_system("mute");
  CHECK(eventually(device, [&] { return device.starts.load() == 2; }));
  CHECK(eventually(device, [&] { return device.stops.load() == 2; }, 15s));
  settings.volume = 0;
  engine.set_settings(settings);
  engine.play_system("unmute");
  engine.preview_notification("cloud_chat");
  std::this_thread::sleep_for(100ms);
  CHECK(device.starts == 2);
  engine.stop();
}

void an_unknown_cue_is_silent() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  engine.set_settings(full_volume());
  engine.start();
  engine.play_notification("none");
  std::this_thread::sleep_for(100ms);
  CHECK(device.opens == 0);
  engine.stop();
}

void idle_mute_silences_everyone_until_lifted() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  Settings settings = full_volume();
  settings.listen_self = true;
  settings.idle_muted = true;
  engine.set_settings(settings);
  engine.start();
  engine.play_peer(1, Kind::KeySpace);
  engine.play_own(Kind::KeySpace);
  std::this_thread::sleep_for(100ms);
  CHECK(device.opens == 0);
  settings.idle_muted = false;
  engine.set_settings(settings);
  engine.play_peer(1, Kind::KeySpace);
  CHECK(eventually(device, [&] { return device.starts.load() == 1; }));
  engine.stop();
}

void picking_a_device_reopens_on_it() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  Settings settings = full_volume();
  engine.set_settings(settings);
  engine.start();
  engine.play_peer(1, Kind::KeySpace);
  CHECK(eventually(device, [&] { return device.starts.load() == 1; }));
  settings.device = "speaker-2";
  engine.set_settings(settings);
  CHECK(eventually(device, [&] { return device.closes.load() == 1 && device.selected() == "speaker-2"; }));
  CHECK(device.opens == 1);  // not until something plays
  engine.play_peer(1, Kind::KeySpace);
  CHECK(eventually(device, [&] { return device.opens.load() == 2 && device.starts.load() == 2; }));
  // Changing anything else leaves the stream alone.
  settings.volume = 50;
  engine.set_settings(settings);
  (void)loudest(device, 50ms);
  CHECK(device.closes == 1);
  engine.stop();
}

void a_checked_device_reports_failure() {
  FakeDevice device;
  device.fail_open = true;
  Engine engine(device, CRPP_SOUNDS_DIR);
  std::atomic<int> failures{0};
  engine.set_on_open_failed([&] { ++failures; });
  engine.set_settings(full_volume());
  engine.start();
  engine.check_device();
  CHECK(eventually(device, [&] { return failures.load() == 1; }));
  // A sound failing to open later is not reported as a bad choice.
  engine.play_peer(1, Kind::KeySpace);
  std::this_thread::sleep_for(50ms);
  CHECK(failures == 1);
  device.fail_open = false;
  Settings settings = full_volume();
  settings.device = "speaker-2";  // a new choice is tried at once, without the retry wait
  engine.set_settings(settings);
  engine.check_device();
  CHECK(eventually(device, [&] { return device.opens.load() == 2; }));
  CHECK(failures == 1);
  CHECK(device.starts == 0);  // opened, but nothing to play
  engine.stop();
}

// set_settings from another thread while sounds play and the device renders.
void settings_may_change_at_any_time() {
  FakeDevice device;
  Engine engine(device, CRPP_SOUNDS_DIR);
  engine.set_settings(full_volume());
  engine.set_members({{1, "", "", "", "cid-a"}});
  engine.start();
  std::atomic<bool> done{false};
  std::thread changer([&] {
    Settings settings = full_volume();
    for (int i = 0; !done && i < 2000; ++i) {
      settings.volume = 1 + i % 100;
      settings.spatial = i % 2 == 0;
      settings.balance = 55 + i % 36;
      settings.keyboard_pack = i % 3 == 0 ? "walnut" : "plastic";
      engine.set_settings(settings);
      engine.set_member_volume("cid-a", i % 101);
      std::this_thread::sleep_for(1ms);
    }
  });
  const auto end = std::chrono::steady_clock::now() + 300ms;
  while (std::chrono::steady_clock::now() < end) {
    engine.play_peer(1, Kind::KeySpace);
    (void)device.pump(512);
    std::this_thread::sleep_for(1ms);
  }
  done = true;
  changer.join();
  engine.stop();
  CHECK(device.closes.load() >= 1);
  CHECK(device.opens.load() >= 1);
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
  a_member_at_zero_is_silent_and_stays_so_across_reconnects();
  a_member_volume_scales_only_that_member();
  clearing_member_volumes_forgets_them();
  an_expected_file_that_is_absent_plays_a_generated_sound();
  a_sound_the_pack_does_not_have_stays_silent();
  an_absent_notification_plays_a_generated_cue();
  chimes_take_the_place_of_a_skins_notifications();
  notifications_follow_the_mute_settings();
  previews_and_system_sounds_need_only_the_volume();
  an_unknown_cue_is_silent();
  idle_mute_silences_everyone_until_lifted();
  picking_a_device_reopens_on_it();
  a_checked_device_reports_failure();
  settings_may_change_at_any_time();
  return crpp_test_result();
}
