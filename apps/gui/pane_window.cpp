// The other windows of a Skin that has them: each panel on a window of its own, owned by the
// main one. App says which should exist, and this makes that true.
#include <windowsx.h>

#include <algorithm>

#include "shell.hpp"
#include "tray.hpp"

using namespace crpp::ui;

namespace crpp::gui {

namespace {

constexpr UINT_PTR pane_blink_timer = 1;
constexpr const wchar_t* pane_class = L"CrppPane";

void save_pane_size(const PaneWindow& w) {
  Shell& s = *w.shell;
  RECT rc;
  const PaneInfo info = s.app.pane_info(w.pane);
  if (!s.store || !info.resizable || !GetClientRect(w.hwnd, &rc) || rc.right <= 0 || rc.bottom <= 0) return;
  s.store->set(saved_size_key(info.key, "Width"), store::Json(static_cast<std::int64_t>(MulDiv(rc.right, 96, static_cast<int>(w.dpi)))));
  s.store->set(saved_size_key(info.key, "Height"), store::Json(static_cast<std::int64_t>(MulDiv(rc.bottom, 96, static_cast<int>(w.dpi)))));
}

void ensure_pane_target(PaneWindow& w) {
  if (w.target) return;
  RECT rc;
  GetClientRect(w.hwnd, &rc);
  const auto dpi = static_cast<float>(w.dpi);
  const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi, dpi);
  const auto hwnd_props = D2D1::HwndRenderTargetProperties(w.hwnd, D2D1::SizeU(static_cast<UINT32>(rc.right), static_cast<UINT32>(rc.bottom)));
  if (ID2D1Factory* const factory = w.shell->app.factory()) factory->CreateHwndRenderTarget(props, hwnd_props, w.target.put());
}

void sync_pane_blink(PaneWindow& w) {
  const bool wanted = w.shell->app.wants_blink() && GetFocus() == w.hwnd;
  if (wanted == w.blink_on) return;
  if (wanted) SetTimer(w.hwnd, pane_blink_timer, 530, nullptr);
  else KillTimer(w.hwnd, pane_blink_timer);
  w.blink_on = wanted;
}

LRESULT CALLBACK pane_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* w = reinterpret_cast<PaneWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  if (!w || !w->hwnd) return DefWindowProcW(hwnd, msg, wp, lp);
  Shell& s = *w->shell;
  const PaneScope scope(s.app, w->pane);
  // What happens here may show in the other windows too, unless it is only the pointer moving.
  const auto changed = [&](bool needed, bool only_here = false) {
    if (needed) {
      InvalidateRect(hwnd, nullptr, FALSE);
      if (!only_here) {
        InvalidateRect(s.hwnd, nullptr, FALSE);
        invalidate_panes(s);
      }
    }
    sync_frames(s);
  };
  const auto dips = [&](int pixels) { return static_cast<float>(pixels) * 96 / static_cast<float>(w->dpi); };
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;
    case WM_NCCALCSIZE:
      if (wp) return 0;  // the whole window is client area
      break;
    case WM_NCPAINT:
      return 0;
    case WM_NCACTIVATE:
      s.app.set_pane_active(w->pane, wp != FALSE);
      InvalidateRect(hwnd, nullptr, FALSE);
      return DefWindowProcW(hwnd, msg, wp, -1);  // -1: the system draws no frame of its own
    case WM_ACTIVATE:
      if (LOWORD(wp) == WA_INACTIVE) changed(s.app.window_deactivated());
      break;
    case WM_NCHITTEST: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &pt);
      RECT rc;
      GetClientRect(hwnd, &rc);
      const PaneInfo info = s.app.pane_info(w->pane);
      return frame_hit_code(WindowFrame::zone_at(dips(pt.x), dips(pt.y), dips(rc.right), dips(rc.bottom), {.resizable = info.resizable, .minimize = false, .close = info.close}, w->shell->app.frame_metrics()));
    }
    case WM_CLOSE:
      s.app.close_pane(w->pane);
      changed(true);
      return 0;
    case WM_GETMINMAXINFO: {
      const PaneInfo info = s.app.pane_info(w->pane);
      reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {MulDiv(static_cast<int>(info.min_w), static_cast<int>(w->dpi), 96), MulDiv(static_cast<int>(info.min_h), static_cast<int>(w->dpi), 96)};
      return 0;
    }
    case WM_SIZE:
      if (w->target) w->target->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
      set_frame_region(hwnd, w->dpi, w->shell->app.frame_metrics().radius);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_EXITSIZEMOVE:
      save_pane_size(*w);
      break;
    case WM_DPICHANGED: {
      w->dpi = HIWORD(wp);
      if (w->target) w->target->SetDpi(static_cast<float>(w->dpi), static_cast<float>(w->dpi));
      const auto* suggested = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      ensure_pane_target(*w);
      if (w->target) {
        const D2D1_SIZE_F size = w->target->GetSize();
        w->target->BeginDraw();
        s.app.paint(w->target.get(), size.width, size.height);
        if (w->target->EndDraw() == D2DERR_RECREATE_TARGET) {
          w->target.reset();
          s.app.ui().forget_render_target();
        }
      }
      EndPaint(hwnd, &ps);
      place_ime(s, hwnd, w->dpi);
      sync_pane_blink(*w);
      if (s.app.ui().more_frames) InvalidateRect(hwnd, nullptr, FALSE);  // it is measuring itself
      return 0;
    }
    case WM_MOUSEMOVE:
      if (!w->tracking) {
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&t);
        w->tracking = true;
      }
      changed(s.app.mouse_move(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp))), true);
      return 0;
    case WM_MOUSELEAVE:
      w->tracking = false;
      changed(s.app.mouse_leave(), true);
      return 0;
    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      SetCapture(hwnd);
      changed(s.app.mouse_down(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp)), (wp & MK_SHIFT) != 0));
      sync_pane_blink(*w);
      return 0;
    case WM_LBUTTONUP: {
      // Before the capture goes, since losing it calls off a press.
      const bool did = s.app.mouse_up(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp)));
      ReleaseCapture();
      changed(did);
      sync_pane_blink(*w);
      return 0;
    }
    case WM_CAPTURECHANGED:
      changed(s.app.mouse_cancel());
      return 0;
    case WM_RBUTTONDOWN:
      changed(s.app.right_click(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp))));
      return 0;
    case WM_MOUSEWHEEL: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &pt);
      changed(s.app.wheel(dips(pt.x), dips(pt.y), static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA), true);
      return 0;
    }
    case WM_SETCURSOR:
      if (apply_cursor(s, LOWORD(lp), w->dpi)) return TRUE;
      break;
    case WM_CHAR:
      changed(s.app.char_input(static_cast<wchar_t>(wp), (GetKeyState(VK_SHIFT) & 0x8000) != 0));
      return 0;
    case WM_KEYDOWN:
      changed(s.app.key_down(static_cast<unsigned>(wp), (GetKeyState(VK_CONTROL) & 0x8000) != 0, (GetKeyState(VK_SHIFT) & 0x8000) != 0));
      return 0;
    case WM_IME_STARTCOMPOSITION:
      place_ime(s, hwnd, w->dpi);
      return 0;  // our own drawing replaces the IME's composition window
    case WM_IME_COMPOSITION: {
      HIMC imc = ImmGetContext(hwnd);
      std::wstring result, comp;
      if (imc) {
        if (lp & GCS_RESULTSTR) result = ime_string(imc, GCS_RESULTSTR);
        if (lp & GCS_COMPSTR) comp = ime_string(imc, GCS_COMPSTR);
        ImmReleaseContext(hwnd, imc);
      }
      changed(s.app.ime_composition(comp, result));
      return 0;
    }
    case WM_IME_ENDCOMPOSITION:
      changed(s.app.ime_composition({}, {}));
      return 0;
    case WM_TIMER:
      if (wp == pane_blink_timer) changed(s.app.blink(), true);
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, pane_blink_timer);
      save_pane_size(*w);
      w->blink_on = w->tracking = false;
      w->target.reset();
      w->hwnd = nullptr;
      s.app.forget_pane(w->pane);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void register_pane_class() {
  static const bool done = [] {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = pane_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIcon = load_app_icon(GetSystemMetrics(SM_CXICON));
    wc.hIconSm = load_app_icon(GetSystemMetrics(SM_CXSMICON));
    wc.lpszClassName = pane_class;
    return RegisterClassExW(&wc) != 0;
  }();
  (void)done;
}

// The size in pixels this pane wants: what was left for one that resizes.
SIZE wanted_size(Shell& s, const PaneInfo& info, UINT dpi) {
  const auto scaled = [&](std::int64_t dips) { return MulDiv(static_cast<int>(dips), static_cast<int>(dpi), 96); };
  if (!info.resizable || !s.store) return {scaled(static_cast<std::int64_t>(info.w)), scaled(static_cast<std::int64_t>(info.h))};
  return {scaled(std::max(static_cast<std::int64_t>(info.min_w), s.store->i64_or(saved_size_key(info.key, "Width"), static_cast<std::int64_t>(info.w)))),
          scaled(std::max(static_cast<std::int64_t>(info.min_h), s.store->i64_or(saved_size_key(info.key, "Height"), static_cast<std::int64_t>(info.h))))};
}

// Over the main window, as far as the monitor allows.
POINT centred_on_main(const Shell& s, SIZE size) {
  RECT main{};
  GetWindowRect(s.hwnd, &main);
  LONG x = main.left + (main.right - main.left - size.cx) / 2, y = main.top + (main.bottom - main.top - size.cy) / 2;
  MONITORINFO info{sizeof info};
  if (GetMonitorInfoW(MonitorFromWindow(s.hwnd, MONITOR_DEFAULTTONEAREST), &info)) {
    x = std::max(info.rcWork.left, std::min(x, info.rcWork.right - size.cx));
    y = std::max(info.rcWork.top, std::min(y, info.rcWork.bottom - size.cy));
  }
  return {x, y};
}

void create_pane(Shell& s, PaneWindow& w) {
  register_pane_class();
  const PaneInfo info = s.app.pane_info(w.pane);
  w.shell = &s;
  w.dpi = s.dpi;
  const SIZE size = wanted_size(s, info, w.dpi);
  const POINT at = centred_on_main(s, size);
  const DWORD style = WS_CAPTION | WS_SYSMENU | (info.resizable ? WS_THICKFRAME : 0);
  // w.hwnd stays empty until this returns: nothing is handled while the window is being made.
  HWND hwnd = CreateWindowExW(0, pane_class, widen(info.title).c_str(), style, at.x, at.y, size.cx, size.cy, s.hwnd, nullptr, GetModuleHandleW(nullptr), &w);
  if (!hwnd) return;
  w.hwnd = hwnd;
  w.fitted_w = info.w;
  w.fitted_h = info.h;
  w.dpi = GetDpiForWindow(hwnd);
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  set_frame_region(hwnd, w.dpi, w.shell->app.frame_metrics().radius);
  ShowWindow(hwnd, w.pane == Pane::Offline ? SW_SHOWNOACTIVATE : SW_SHOW);
}

}  // namespace

void invalidate_panes(Shell& s) {
  for (const PaneWindow& w : s.panes) {
    if (w.hwnd) InvalidateRect(w.hwnd, nullptr, FALSE);
  }
}

void drop_pane_targets(Shell& s) {
  for (PaneWindow& w : s.panes) w.target.reset();
}

void request_pane_sync(Shell& s) {
  PaneState wanted = s.app.pane_state();
  wanted.hidden = s.hidden;
  if (wanted == s.panes_made || s.panes_posted || !s.hwnd) return;
  s.panes_posted = true;
  PostMessageW(s.hwnd, wm_sync_panes, 0, 0);
}

void sync_panes(Shell& s) {
  s.panes_posted = false;
  const auto open = s.app.open_panes();
  const auto is_open = [&](Pane pane) { return std::ranges::find(open, pane) != open.end(); };
  // Enabled again first: a window closing hands the keyboard to its owner.
  const bool modal = is_open(Pane::Dialog);
  EnableWindow(s.hwnd, modal ? FALSE : TRUE);
  for (std::size_t i = 1; i < s.panes.size(); ++i) {
    PaneWindow& w = s.panes[i];
    if (w.hwnd && static_cast<Pane>(i) != Pane::Dialog) EnableWindow(w.hwnd, modal ? FALSE : TRUE);
  }
  for (std::size_t i = 1; i < s.panes.size(); ++i) {
    PaneWindow& w = s.panes[i];
    w.pane = static_cast<Pane>(i);
    if (w.hwnd && !is_open(w.pane)) DestroyWindow(w.hwnd);
  }
  if (!s.hidden) {
    for (std::size_t i = 1; i < s.panes.size(); ++i) {
      PaneWindow& w = s.panes[i];
      if (!is_open(w.pane)) continue;
      if (!w.hwnd) {
        create_pane(s, w);
        if (w.hwnd && modal && w.pane != Pane::Dialog) EnableWindow(w.hwnd, FALSE);
      } else if (!IsWindowVisible(w.hwnd)) {
        ShowWindow(w.hwnd, SW_SHOWNOACTIVATE);
      }
      const PaneInfo info = s.app.pane_info(w.pane);
      if (w.hwnd && !info.resizable && (info.w != w.fitted_w || info.h != w.fitted_h)) {
        // Another question in the same window.
        const SIZE size = wanted_size(s, info, w.dpi);
        w.fitted_w = info.w;
        w.fitted_h = info.h;
        SetWindowPos(w.hwnd, nullptr, 0, 0, size.cx, size.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
      if (w.hwnd) InvalidateRect(w.hwnd, nullptr, FALSE);
    }
    if (const auto raised = s.app.take_raised_pane()) {
      const PaneWindow& w = s.panes[static_cast<std::size_t>(*raised)];
      if (w.hwnd) SetForegroundWindow(w.hwnd);
    }
  } else {
    for (PaneWindow& w : s.panes) {
      if (w.hwnd) ShowWindow(w.hwnd, SW_HIDE);
    }
    drop_pane_targets(s);
  }
  s.panes_made = s.app.pane_state();
  s.panes_made.hidden = s.hidden;
}

}  // namespace crpp::gui
