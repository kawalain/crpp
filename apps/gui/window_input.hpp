#pragma once
// The pointer, the keys and the IME, as every window of the app takes them. The main window and
// the panels' windows differ in what a change invalidates and in a few messages of their own; all
// that is here they answer the same way, so they cannot drift apart.
#include <windowsx.h>

#include "shell.hpp"

namespace crpp::gui {

// Handles `msg` if it is one of these; true with `result` set when the window procedure should
// return it. `changed(needed, only_here)` repaints: this window, and the others unless only_here.
// `hold_timer` is the window's own timer id for a held control that repeats; the procedure runs
// App::hold_step when it fires. Call inside the window's PaneScope.
template <typename Changed>
bool handle_input(Shell& s, HWND hwnd, UINT dpi, bool& tracking, UINT_PTR hold_timer, UINT msg, WPARAM wp, LPARAM lp, const Changed& changed,
                  LRESULT& result) {
  const auto dips = [&](int pixels) { return static_cast<float>(pixels) * 96 / static_cast<float>(dpi); };
  const auto shift = [] { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; };
  result = 0;
  switch (msg) {
    case WM_MOUSEMOVE:
      if (!tracking) {
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&t);
        tracking = true;
      }
      changed(s.app.mouse_move(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp))), true);
      return true;
    case WM_MOUSELEAVE:
      tracking = false;
      changed(s.app.mouse_leave(), true);
      return true;
    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      SetCapture(hwnd);
      changed(s.app.mouse_down(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp)), (wp & MK_SHIFT) != 0), false);
      if (const unsigned wait = s.app.hold_wait_ms()) SetTimer(hwnd, hold_timer, wait, nullptr);
      return true;
    case WM_LBUTTONUP: {
      // Before the capture goes, since losing it calls off a press.
      const bool did = s.app.mouse_up(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp)));
      ReleaseCapture();
      changed(did, false);
      return true;
    }
    case WM_CAPTURECHANGED:
      KillTimer(hwnd, hold_timer);
      changed(s.app.mouse_cancel(), false);
      return true;
    case WM_MOUSEWHEEL: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &pt);
      changed(s.app.wheel(dips(pt.x), dips(pt.y), static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA), true);
      return true;
    }
    case WM_RBUTTONUP:
      // As Windows does, on the way up; what is being composed goes in first.
      complete_composition(hwnd);
      changed(s.app.right_click(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp))), false);
      return true;
    case WM_SETCURSOR:
      if (!apply_cursor(s, LOWORD(lp), dpi)) return false;
      result = TRUE;
      return true;
    case WM_CHAR:
      changed(s.app.char_input(static_cast<wchar_t>(wp), shift()), false);
      return true;
    case WM_SYSKEYDOWN:
      if (wp == VK_F10 && shift()) complete_composition(hwnd);
      if (!s.app.sys_key(static_cast<unsigned>(wp), shift())) return false;
      changed(true, false);
      return true;
    case WM_KEYDOWN:
      if (wp == VK_APPS) complete_composition(hwnd);
      changed(s.app.key_down(static_cast<unsigned>(wp), (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift()), false);
      return true;
    case WM_IME_STARTCOMPOSITION:
      place_ime(s, hwnd, dpi);
      return true;  // our own drawing replaces the IME's composition window
    case WM_IME_COMPOSITION: {
      HIMC imc = ImmGetContext(hwnd);
      std::wstring composed, comp;
      if (imc) {
        if (lp & GCS_RESULTSTR) composed = ime_string(imc, GCS_RESULTSTR);
        if (lp & GCS_COMPSTR) comp = ime_string(imc, GCS_COMPSTR);
        ImmReleaseContext(hwnd, imc);
      }
      changed(s.app.ime_composition(comp, composed), false);
      return true;
    }
    case WM_IME_ENDCOMPOSITION:
      changed(s.app.ime_composition({}, {}), false);
      return true;
    default:
      return false;
  }
}

}  // namespace crpp::gui
