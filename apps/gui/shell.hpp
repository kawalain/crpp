#pragma once
// What the parts of the window shell share.
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "bridge.hpp"
#include "crpp/client/client.hpp"
#include "crpp/input/raw_input_source.hpp"
#include "crpp/sound/engine.hpp"
#include "crpp/sound/wasapi_device.hpp"
#include "crpp/store/store.hpp"
#include "crpp/store/windows_platform.hpp"
#include "crpp/transport/winhttp_transport.hpp"
#include "crpp/ui/app.hpp"
#include "diag_log.hpp"
#include "store_prefs.hpp"

namespace crpp::gui {

namespace area::sound {
struct State;
}

inline constexpr UINT_PTR blink_timer = 1;
inline constexpr UINT_PTR frame_timer = 2;
inline constexpr UINT_PTR smoke_timer = 3;
inline constexpr UINT_PTR save_timer = 4;
inline constexpr UINT_PTR wake_timer = 5;
inline constexpr UINT save_delay_ms = 2000;  // chat arrives in bursts; write once per burst
inline constexpr UINT wm_ui_task = WM_APP + 1;
inline constexpr UINT wm_own_input = WM_APP + 2;

inline std::int64_t unix_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Runs closures on the UI thread; the client's callbacks arrive on another.
class UiTasks {
 public:
  void attach(HWND hwnd) { hwnd_ = hwnd; }
  void post(std::function<void()> f) {
    {
      std::lock_guard lock(m_);
      q_.push_back(std::move(f));
    }
    if (hwnd_) PostMessageW(hwnd_, wm_ui_task, 0, 0);
  }
  void run_all() {
    std::deque<std::function<void()>> batch;
    {
      std::lock_guard lock(m_);
      batch.swap(q_);
    }
    for (auto& f : batch) f();
  }

 private:
  HWND hwnd_ = nullptr;
  std::mutex m_;
  std::deque<std::function<void()>> q_;
};

// Every input goes to the server and, if the user wants, to their own speakers.
class InputSink final : public input::Sink {
 public:
  InputSink(std::unique_ptr<client::Client>& client, std::unique_ptr<sound::Engine>& engine)
      : client_(client), engine_(engine) {}
  void attach(HWND hwnd) noexcept { hwnd_ = hwnd; }
  void on_input(wire::Kind kind) noexcept override {
    client_->input(kind);
    engine_->play_own(kind);
    // The server never sends a member's input back, so their ring is lit from
    // here. One message at a time, and no heap on this path.
    if (!pulse_posted_.exchange(true, std::memory_order_relaxed)) PostMessageW(hwnd_, wm_own_input, 0, 0);
  }
  // UI thread: the next input may post again.
  void pulse_taken() noexcept { pulse_posted_.store(false, std::memory_order_relaxed); }

 private:
  std::unique_ptr<client::Client>& client_;
  std::unique_ptr<sound::Engine>& engine_;
  HWND hwnd_ = nullptr;
  std::atomic<bool> pulse_posted_{false};
};

struct ConnectionArea;  // area_connection.cpp

struct Shell {
  ui::App app;
  UiTasks tasks;
  HWND hwnd = nullptr;
  ui::ComPtr<ID2D1HwndRenderTarget> target;
  bool tracking = false;
  bool frame_timer_on = false;
  bool blink_timer_on = false;
  bool hidden = true;       // hidden or minimized: no render resources, no frames
  UINT dpi = USER_DEFAULT_SCREEN_DPI;
  std::filesystem::path assets_dir;
  std::string asset_url;
  std::thread assets_worker;
  std::wstring smoke_path;  // --smoke: create a room, save a screenshot, leave, exit
  ULONGLONG smoke_start = 0;
  bool smoke_left = false;
  bool save_timer_on = false;
  store::WindowsPlatform platform;
  std::optional<store::Store> store;
  std::optional<StorePrefs> prefs;
  DiagLog log;
  client::Config config;
  std::unique_ptr<transport::WinHttpTransport> transport;
  std::unique_ptr<Bridge> bridge;
  std::unique_ptr<client::Client> client;
  sound::WasapiDevice audio_device;
  sound::Settings audio;
  std::unique_ptr<sound::Engine> engine;
  InputSink input_sink{client, engine};
  std::unique_ptr<input::RawInputSource> input;
  std::shared_ptr<area::sound::State> sound_state;
  bool throwaway_data = false;  // --data: a scripted or test run
  std::shared_ptr<ConnectionArea> connection;  // last: stops its thread before the rest goes
};

// --- startup.cpp ---
void start_client(Shell& s);
void enter_app(Shell& s);
void start_install(Shell& s);

// --- window.cpp ---
HWND create_window(Shell& s);
// Runs the frame timer while an effect is animating, and a single wake-up for the
// next one that is only waiting.
void sync_frames(Shell& s);

// --- shot.cpp ---
// `--shot out.png [WxH] [hour] [scene] [notice]`; the exit code of the process.
int run_shot(Shell& s, int argc, char** argv, float dpi);

}  // namespace crpp::gui
