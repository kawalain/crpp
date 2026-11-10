#pragma once
// The notification-area icon and its notifications (Shell_NotifyIcon).
//
// The icon is identified by window and id, not by GUID: Windows binds a GUID to the
// path of the binary, and this app is one exe that can live anywhere.
#include <windows.h>

#include <d2d1.h>

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

#include "crpp/ui/gfx.hpp"
#include "crpp/win/handles.hpp"

namespace crpp::gui {

inline constexpr UINT wm_tray = WM_APP + 0x100;  // LOWORD(lParam) is the event
inline constexpr UINT tray_icon_id = 1;
inline constexpr int app_icon_id = 1;  // crpp.rc.in

// Draws into a target of `size` pixels and returns straight-alpha BGRA pixels, top row
// first; empty on failure.
[[nodiscard]] std::vector<std::uint32_t> render_pixels(ID2D1Factory* factory, int size,
                                                       const std::move_only_function<void(ID2D1RenderTarget*) const>& draw);
// The icon in the exe, at that size in pixels; null without one.
[[nodiscard]] win::Icon load_app_icon(int size);
// Null on failure.
[[nodiscard]] win::Icon icon_from_pixels(const std::vector<std::uint32_t>& pixels, int size);

class TrayIcon {
 public:
  void attach(HWND hwnd) { hwnd_ = hwnd; }
  // False where no taskbar takes it. Does nothing if it is already there.
  bool add();
  void remove();
  // The taskbar restarted or changed DPI: what it had is gone.
  void forget() { present_ = false; }
  [[nodiscard]] bool present() const { return present_; }

  // Windows queues these. `balloon_icon` may be null, and is copied.
  bool notify(std::wstring_view title, std::wstring_view body, HICON balloon_icon, DWORD flags);
  // Takes back what is still on screen.
  void clear_notification();

 private:
  HWND hwnd_ = nullptr;
  bool present_ = false;
};

}  // namespace crpp::gui
