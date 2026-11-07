#pragma once
// What the parts of the window shell share.
#include <windows.h>

#include <array>
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
#include "crpp/assets/assets.hpp"
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

using locale::tr;

namespace area::sound {
struct State;
}

inline constexpr UINT_PTR blink_timer = 1;
inline constexpr UINT_PTR frame_timer = 2;
inline constexpr UINT_PTR smoke_timer = 3;
inline constexpr UINT_PTR save_timer = 4;
inline constexpr UINT_PTR wake_timer = 5;
inline constexpr UINT_PTR hold_timer = 6;
inline constexpr UINT save_delay_ms = 2000;  // chat arrives in bursts; write once per burst
inline constexpr UINT wm_ui_task = WM_APP + 1;
inline constexpr UINT wm_own_input = WM_APP + 2;
inline constexpr UINT wm_exit = WM_APP + 3;
inline constexpr UINT wm_sync_panes = WM_APP + 4;

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
  // For a task that asks for what it needs drawn itself, so it costs no full repaint.
  void post_quiet(std::function<void()> f) {
    {
      std::lock_guard lock(m_);
      q_.push_back(std::move(f));
      ++quiet_;
    }
    if (hwnd_) PostMessageW(hwnd_, wm_ui_task, 0, 0);
  }
  // True if any of them may have changed what is on screen.
  bool run_all() {
    std::deque<std::function<void()>> batch;
    bool loud = false;
    {
      std::lock_guard lock(m_);
      batch.swap(q_);
      loud = batch.size() > quiet_;
      quiet_ = 0;
    }
    for (auto& f : batch) f();
    return loud;
  }

 private:
  HWND hwnd_ = nullptr;
  std::mutex m_;
  std::deque<std::function<void()>> q_;
  std::size_t quiet_ = 0;
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
struct Shell;

// A panel's own window, while it has one.
struct PaneWindow {
  Shell* shell = nullptr;
  ui::Pane pane = ui::Pane::Main;
  HWND hwnd = nullptr;
  ui::ComPtr<ID2D1HwndRenderTarget> target;
  UINT dpi = USER_DEFAULT_SCREEN_DPI;
  bool tracking = false, blink_on = false;
  float fitted_w = 0, fitted_h = 0;
};

// A system cursor made from the pack, for one size.
struct MadeCursor {
  skin::CursorKind kind;
  int size;
  HCURSOR cursor;
};

struct Shell {
  ui::App app;
  std::vector<MadeCursor> cursors;
  UiTasks tasks;
  HWND hwnd = nullptr;
  ui::ComPtr<ID2D1HwndRenderTarget> target;
  bool tracking = false;
  bool frame_timer_on = false;
  bool frame_running = false;           // something moves, so each frame asks for the next
  unsigned long long last_frame_ms = 0;
  bool blink_timer_on = false;
  bool hidden = true;       // hidden or minimized: no render resources, no frames
  // --release-unused: a shown window lets go of the graphics device after this long with
  // nothing on it and another program in front. 0, off, until seen on a real desktop.
  UINT unused_release_ms = 0;
  UINT dpi = USER_DEFAULT_SCREEN_DPI;
  bool own_frame = false;     // the window as it was last set up for the Skin
  std::string fitted_screen;  // which screen's size the window has; empty: the one size of all
  std::array<PaneWindow, ui::pane_count> panes;  // by Pane; the first is not used
  ui::PaneState panes_made;
  bool panes_posted = false;
  std::filesystem::path assets_dir;
  assets::Manifest asset_manifest;
  std::string asset_url;
  std::thread assets_worker;
  std::vector<std::string> expected_sounds;  // what the manifest promises, on disk or not
  std::filesystem::path data_dir;  // the Protected store's folder
  std::wstring data_override;      // --data, so a restart keeps it
  std::string update_url;          // --update-url, only for builds that are not official
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
  std::shared_ptr<void> account;  // area_account.cpp's own state
  bool throwaway_data = false;  // --data: a scripted or test run
  bool shares_the_machine = false;  // os_policy::shares_the_machine
  std::shared_ptr<ConnectionArea> connection;  // last: stops its thread before the rest goes
};

// --- startup.cpp ---
void start_client(Shell& s);
void enter_app(Shell& s);
// Fetches what is missing. With wait, the first start's progress screen stays until that
// is done; otherwise it goes on behind the app, for the next start.
void start_install(Shell& s, bool wait);
// A nickname like the upstream one: "<adjective> <animal>".
std::string random_nickname(const std::filesystem::path& words);

// --- window.cpp ---
HWND create_window(Shell& s);
// The Skin may have changed: makes the window's frame and size what it wants.
void frame_changed(Shell& s);
// Runs the frame timer while an effect is animating, and a single wake-up for the
// next one that is only waiting.
void sync_frames(Shell& s);
// For the other windows of a Skin's own frame.
[[nodiscard]] std::string saved_size_key(std::string_view screen, std::string_view side);
void set_frame_region(HWND hwnd, UINT dpi, float radius_dips);
// Answers WM_SETCURSOR for this hit test. False: the system's default is right.
[[nodiscard]] bool apply_cursor(Shell& s, UINT hit, UINT dpi);
// The setting changed: whatever is under the pointer shows it now.
void reapply_cursor(Shell& s);
// The look or the system's pointer settings changed.
void forget_cursors(Shell& s);
[[nodiscard]] LRESULT frame_hit_code(ui::WindowFrame::Zone zone);
void place_ime(Shell& s, HWND hwnd, UINT dpi);
[[nodiscard]] std::wstring ime_string(HIMC imc, DWORD kind);

// --- pane_window.cpp ---
// Asks for the windows to be brought in line with App, once it is safe to destroy one.
void request_pane_sync(Shell& s);
void sync_panes(Shell& s);
void invalidate_panes(Shell& s);
// Before App::release_graphics, and whenever nobody sees them.
void drop_pane_targets(Shell& s);

// --- shot.cpp ---
// `--shot out.png [WxH] [hour] [scene] [notice]`; the exit code of the process.
int run_shot(Shell& s, int argc, char** argv, float dpi);

}  // namespace crpp::gui
