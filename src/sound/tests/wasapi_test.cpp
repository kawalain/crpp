// Runs on the real default output, rendering silence. A machine with no output
// device has nothing to test.
#include "crpp/sound/wasapi_device.hpp"

#include <atomic>
#include <cstdio>
#include <thread>

#include "check.hpp"

using namespace crpp::sound;
using namespace std::chrono_literals;

namespace {

class Silence final : public Renderer {
 public:
  void render(std::span<float> stereo) noexcept override {
    for (float& v : stereo) v = 0;
    frames.fetch_add(stereo.size() / 2);
  }
  std::atomic<std::size_t> frames{0};
};

void streams_stop_and_reopen() {
  WasapiDevice device;
  Silence silence;
  const auto rate = device.open(silence);
  if (!rate) {
    std::printf("no usable output device; skipped\n");
    return;
  }
  CHECK(*rate >= 8000 && *rate <= 768000);
  CHECK(device.start());
  CHECK(device.start());  // already running
  std::this_thread::sleep_for(300ms);
  device.stop();
  const std::size_t rendered = silence.frames.load();
  // At least the 300 ms, less a little slack for the engine's timing.
  CHECK(rendered >= *rate / 5);
  std::this_thread::sleep_for(50ms);
  CHECK(silence.frames.load() == rendered);

  CHECK(device.start());
  std::this_thread::sleep_for(100ms);
  CHECK(silence.frames.load() > rendered);
  device.stop();
  device.stop();
  device.close();
  device.close();

  CHECK(device.open(silence).has_value());
  CHECK(device.start());
  std::this_thread::sleep_for(50ms);
  // Closing while running is allowed too.
  device.close();
}

void not_opened_means_not_started() {
  WasapiDevice device;
  CHECK(!device.start());
  device.stop();
  device.close();
}

}  // namespace

int main() {
  not_opened_means_not_started();
  streams_stop_and_reopen();
  return crpp_test_result();
}
