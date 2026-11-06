// Drives the real adapter with injected input. F24 and the wheel do nothing
// visible on a normal desktop.
#include "crpp/input/raw_input_source.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "check.hpp"

using namespace crpp::input;
using crpp::wire::Kind;

namespace {

constexpr WORD scan_f24 = 0x76;

class Recorder final : public Sink {
 public:
  void on_input(Kind kind) noexcept override {
    last.store(kind);
    count.fetch_add(1);
  }
  std::atomic<Kind> last{Kind::MouseLeft};
  std::atomic<int> count{0};
};

void send_key(WORD scan, bool up) {
  INPUT in{};
  in.type = INPUT_KEYBOARD;
  in.ki.wScan = scan;
  in.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0u);
  SendInput(1, &in, sizeof in);
}

void send_wheel() {
  INPUT in{};
  in.type = INPUT_MOUSE;
  in.mi.dwFlags = MOUSEEVENTF_WHEEL;
  in.mi.mouseData = 0;
  SendInput(1, &in, sizeof in);
}

bool wait_for(const std::atomic<int>& count, int at_least) {
  for (int i = 0; i < 200; ++i) {
    if (count.load() >= at_least) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  return false;
}

void reports_while_running_and_not_after() {
  Recorder recorder;
  RawInputSource source(recorder);
  CHECK(source.start());
  CHECK(source.start());
  send_key(scan_f24, false);
  send_key(scan_f24, true);
  CHECK(wait_for(recorder.count, 1));
  CHECK(recorder.last.load() == Kind::KeyAny);

  source.stop();
  const int seen = recorder.count.load();
  send_key(scan_f24, false);
  send_key(scan_f24, true);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  CHECK(recorder.count.load() == seen);

  // It can start again, as every Room entry does.
  CHECK(source.start());
  send_wheel();
  CHECK(wait_for(recorder.count, seen + 1));
  CHECK(recorder.last.load() == Kind::MouseWheel);
  source.stop();
  source.stop();
}

}  // namespace

int main() {
  reports_while_running_and_not_after();
  return crpp_test_result();
}
