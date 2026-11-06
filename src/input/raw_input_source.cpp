#include "crpp/input/raw_input_source.hpp"

#include <windows.h>

#include <array>
#include <cstdint>
#include <thread>

namespace crpp::input {

namespace {
constexpr DWORD batch_ms = 10;  // no more than the audio period a sound waits for anyway
constexpr std::size_t buffered_records = 32;
using QWORD = unsigned long long;  // NEXTRAWINPUTBLOCK names it, and the SDK leaves it to the caller
}  // namespace

struct RawInputSource::Impl {
  Sink& sink;
  std::atomic<Privacy>& privacy;
  std::thread thread;
  DWORD thread_id = 0;
  KeyClassifier classifier;

  // Reads everything queued in one call per batch and takes the matching
  // WM_INPUT messages off the queue. False when the read failed.
  bool drain() noexcept {
    // Large enough for keyboard and mouse; other devices are not registered.
    alignas(8) std::array<unsigned char, buffered_records * sizeof(RAWINPUT)> buffer;
    bool ok = true;
    for (;;) {
      UINT size = static_cast<UINT>(buffer.size());
      auto* raw = reinterpret_cast<RAWINPUT*>(buffer.data());
      const UINT count = GetRawInputBuffer(raw, &size, sizeof(RAWINPUTHEADER));
      if (count == static_cast<UINT>(-1)) ok = false;
      if (count == static_cast<UINT>(-1) || count == 0) break;
      for (UINT i = 0; i < count; ++i, raw = NEXTRAWINPUTBLOCK(raw)) {
        if (raw->header.dwType == RIM_TYPEKEYBOARD) on_keyboard(raw->data.keyboard);
        else if (raw->header.dwType == RIM_TYPEMOUSE) on_mouse(raw->data.mouse);
      }
    }
    SecureZeroMemory(buffer.data(), buffer.size());
    return ok;
  }

  void on_keyboard(const RAWKEYBOARD& key) noexcept {
    // 0xFF is a fake make code that some drivers send with a real key's prefix.
    if (key.MakeCode == 0xFF || (key.Flags & RI_KEY_E1) != 0) return;
    classifier.set_privacy(privacy.load(std::memory_order_relaxed));
    const auto kind = classifier.on_key(key.MakeCode, (key.Flags & RI_KEY_E0) != 0, (key.Flags & RI_KEY_BREAK) != 0);
    if (kind) sink.on_input(*kind);
  }

  void on_mouse(const RAWMOUSE& mouse) noexcept {
    const USHORT flags = mouse.usButtonFlags;
    if (flags == 0) return;  // plain movement, the usual case
    const auto report = [&](USHORT flag, MouseAction action) {
      if ((flags & flag) != 0) sink.on_input(mouse_kind(action));
    };
    report(RI_MOUSE_BUTTON_1_DOWN, MouseAction::LeftDown);
    report(RI_MOUSE_BUTTON_1_UP, MouseAction::LeftUp);
    report(RI_MOUSE_BUTTON_2_DOWN, MouseAction::RightDown);
    report(RI_MOUSE_BUTTON_2_UP, MouseAction::RightUp);
    report(RI_MOUSE_BUTTON_3_DOWN, MouseAction::MiddleDown);
    report(RI_MOUSE_BUTTON_3_UP, MouseAction::MiddleUp);
    report(RI_MOUSE_BUTTON_4_DOWN, MouseAction::BackDown);
    report(RI_MOUSE_BUTTON_5_DOWN, MouseAction::ForwardDown);
    report(RI_MOUSE_WHEEL, MouseAction::Wheel);
  }

  void run(std::atomic<int>& result) noexcept {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = L"CrppRawInput";
    RegisterClassExW(&wc);  // already registered after the first start
    const HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    std::array<RAWINPUTDEVICE, 2> devices{};
    if (hwnd) {
      devices[0] = {0x01, 0x06, RIDEV_INPUTSINK, hwnd};  // generic desktop: keyboard
      devices[1] = {0x01, 0x02, RIDEV_INPUTSINK, hwnd};  // generic desktop: mouse
    }
    const bool registered = hwnd && RegisterRawInputDevices(devices.data(), static_cast<UINT>(devices.size()), sizeof devices[0]);
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);  // so stop() can post here
    result.store(registered ? 1 : -1);
    result.notify_all();
    if (registered) {
      // A moving mouse reports up to thousands of times a second and every
      // report is dropped here, so reading them one by one costs real CPU. The
      // first event after a pause is handled at once; a stream goes in batches.
      // Taking messages by range leaves WM_INPUT for the buffered read.
      bool quit = false;
      const auto pump = [&](UINT first, UINT last) {
        while (PeekMessageW(&msg, nullptr, first, last, PM_REMOVE)) {
          if (msg.message == WM_QUIT) quit = true;
          else DispatchMessageW(&msg);
        }
      };
      while (!quit) {
        MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (!drain()) pump(WM_INPUT, WM_INPUT);  // drop what cannot be read rather than spin on it
        pump(0, WM_INPUT - 1);
        pump(WM_INPUT + 1, 0xFFFFFFFF);
        if (!quit) Sleep(batch_ms);
      }
      devices[0] = {0x01, 0x06, RIDEV_REMOVE, nullptr};
      devices[1] = {0x01, 0x02, RIDEV_REMOVE, nullptr};
      RegisterRawInputDevices(devices.data(), static_cast<UINT>(devices.size()), sizeof devices[0]);
    }
    if (hwnd) DestroyWindow(hwnd);
    classifier.reset();
  }
};

RawInputSource::RawInputSource(Sink& sink) : impl_(new Impl{sink, privacy_, {}, 0, {}}) {}

RawInputSource::~RawInputSource() { stop(); }

bool RawInputSource::start() {
  if (impl_->thread.joinable()) return true;
  std::atomic<int> result{0};
  impl_->thread = std::thread([this, &result] {
    impl_->thread_id = GetCurrentThreadId();
    impl_->run(result);
  });
  result.wait(0);
  if (result.load() > 0) return true;
  impl_->thread.join();
  return false;
}

void RawInputSource::stop() {
  if (!impl_->thread.joinable()) return;
  PostThreadMessageW(impl_->thread_id, WM_QUIT, 0, 0);
  impl_->thread.join();
}

}  // namespace crpp::input
