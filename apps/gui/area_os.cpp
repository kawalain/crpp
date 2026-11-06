// The tray icon, closing to it, and what the window gives up while nobody sees it.
#include <windows.h>

#include <shellapi.h>
#include <windowsx.h>

#include "areas.hpp"
#include "os_policy.hpp"
#include "shell.hpp"
#include "tray.hpp"

using namespace crpp::ui;

namespace crpp::gui::area::os {

namespace {

constexpr const char* hint_shown_key = "roomStaysConnectedShown";  // not upstream's

struct State {
  TrayIcon tray;
  UINT taskbar_created = 0;
};
State state;  // one window per process

[[nodiscard]] bool tray_wanted(const Shell& s) { return s.prefs->get(pref::minimize_to_tray); }

void show_window(Shell& s) {
  ShowWindow(s.hwnd, SW_SHOW);
  if (IsIconic(s.hwnd)) ShowWindow(s.hwnd, SW_RESTORE);
  SetForegroundWindow(s.hwnd);
}

// Input timing keeps being shared with the Room while the window is away.
void hint_room_stays_connected(Shell& s) {
  if (!os_policy::hide_needs_hint(s.app.view() == View::Session, s.store->bool_or(hint_shown_key, false))) return;
  if (!state.tray.notify(L"Cloudroom", L"창을 숨겨도 구름에 계속 연결되어 있어요. 키보드와 마우스 입력 시점도 계속 공유돼요.",
                         nullptr, NIIF_NOSOUND)) {
    return;
  }
  s.store->set(hint_shown_key, store::Json(true));
}

void close_to_tray(Shell& s) {
  ShowWindow(s.hwnd, SW_HIDE);
  hint_room_stays_connected(s);
}

void show_menu(Shell& s, int x, int y) {
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  constexpr UINT_PTR exit_item = 1;
  AppendMenuW(menu, MF_STRING, exit_item, L"Cloudroom 종료");
  // Without both of these the menu does not close on a click elsewhere, or closes
  // at once the second time it opens (TrackPopupMenu's documentation).
  SetForegroundWindow(s.hwnd);
  const BOOL chosen = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, x, y, 0, s.hwnd, nullptr);
  PostMessageW(s.hwnd, WM_NULL, 0, 0);
  DestroyMenu(menu);
  if (static_cast<UINT_PTR>(chosen) == exit_item) DestroyWindow(s.hwnd);
}

void on_tray_event(Shell& s, WPARAM wp, LPARAM lp) {
  switch (LOWORD(lp)) {
    case NIN_SELECT:
    case NIN_KEYSELECT:
      show_window(s);
      break;
    case WM_CONTEXTMENU:
      show_menu(s, GET_X_LPARAM(wp), GET_Y_LPARAM(wp));
      break;
    case NIN_BALLOONUSERCLICK:
      show_window(s);
      break;
    default:
      break;
  }
}

// A hidden window draws nothing. What it drew with comes back on the next WM_PAINT.
void sync_hidden(Shell& s) {
  const bool hidden = os_policy::window_hidden(IsWindowVisible(s.hwnd) != FALSE, IsIconic(s.hwnd) != FALSE);
  if (hidden == s.hidden) return;
  s.hidden = hidden;
  if (hidden) {
    s.target.reset();
    if (s.app.ui().emoji) s.app.ui().emoji.emplace(s.assets_dir);
  } else {
    InvalidateRect(s.hwnd, nullptr, FALSE);
  }
  sync_frames(s);
}

}  // namespace

void wire(Shell& s) {
  state.tray.attach(s.hwnd, s.app.ui().gfx);
  state.taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
  if (tray_wanted(s)) state.tray.add();
}

bool scene(Shell&, std::string_view) { return false; }

void pref_changed(Shell& s, std::string_view key) {
  if (key == pref::minimize_to_tray.name) {
    if (tray_wanted(s)) state.tray.add();
    else state.tray.remove();
  }
}

void first_show(Shell& s, bool minimized) {
  if (!minimized) {
    ShowWindow(s.hwnd, SW_SHOWDEFAULT);
    return;
  }
  if (!os_policy::start_hidden(tray_wanted(s), state.tray.present())) ShowWindow(s.hwnd, SW_SHOWMINNOACTIVE);
}

bool on_message(Shell& s, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
  if (msg == wm_tray) {
    if (HIWORD(lp) == tray_icon_id) on_tray_event(s, wp, lp);
    result = 0;
    return true;
  }
  if (msg != 0 && msg == state.taskbar_created) {
    state.tray.forget();
    if (tray_wanted(s)) state.tray.add();
    result = 0;
    return true;
  }
  switch (msg) {
    case WM_CLOSE:
      // Tried again here: the taskbar may have been busy when it first was.
      if (tray_wanted(s)) state.tray.add();
      if (!os_policy::close_hides(tray_wanted(s), state.tray.present())) return false;
      close_to_tray(s);
      result = 0;
      return true;
    case WM_WINDOWPOSCHANGED:
      sync_hidden(s);
      return false;
    case WM_DESTROY:
      state.tray.remove();
      return false;
    default:
      return false;
  }
}

}  // namespace crpp::gui::area::os
