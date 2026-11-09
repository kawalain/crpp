// The other windows of a Skin that has them: each panel on a window of its own, owned by the
// main one. App says which should exist, and this makes that true.
#include <windowsx.h>

#include <algorithm>

#include "areas.hpp"
#include "shell.hpp"
#include "tray.hpp"
#include "window_input.hpp"

using namespace crpp::ui;

namespace crpp::gui {

namespace {

constexpr UINT_PTR pane_blink_timer = 1;
constexpr UINT_PTR pane_hold_timer = 2;
constexpr const wchar_t* pane_class = L"CrppPane";

// How round a pane's corners are cut: the frame's, or what the Mini window's screen says.
float region_radius(Shell& s, Pane pane) {
  if (pane == Pane::Mini) return s.app.pane_info(pane).radius;
  return s.app.frame_metrics().radius;
}

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
  // The pointer, the keys and the IME, as every window of the app takes them.
  if (LRESULT answer = 0; handle_input(s, hwnd, w->dpi, w->tracking, pane_hold_timer, msg, wp, lp, changed, answer)) {
    sync_pane_blink(*w);
    return answer;
  }
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
      area::os::activation(s, wp, lp);
      if (LOWORD(wp) == WA_INACTIVE) changed(s.app.window_deactivated());
      break;
    case WM_NCHITTEST: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &pt);
      RECT rc;
      GetClientRect(hwnd, &rc);
      // The Mini window has no frame: wherever there is nothing to click, it moves.
      if (w->pane == Pane::Mini) return s.app.mini_drags_at(dips(pt.x), dips(pt.y)) ? HTCAPTION : HTCLIENT;
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
      set_frame_region(hwnd, w->dpi, region_radius(s, w->pane));
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
      else area::os::painted(s);
      return 0;
    }
    case WM_TIMER:
      if (wp == pane_blink_timer) {
        changed(s.app.blink(), true);
      } else if (wp == pane_hold_timer) {
        SetTimer(hwnd, pane_hold_timer, s.app.hold_repeat_ms(), nullptr);
        changed(s.app.hold_step());
      }
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, pane_blink_timer);
      KillTimer(hwnd, pane_hold_timer);
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

// The Mini window starts at the top right of the main window's monitor.
POINT top_right(const Shell& s, SIZE size, UINT dpi) {
  MONITORINFO info{sizeof info};
  const LONG margin = MulDiv(16, static_cast<int>(dpi), 96);
  if (!GetMonitorInfoW(MonitorFromWindow(s.hwnd, MONITOR_DEFAULTTONEAREST), &info)) return {margin, margin};
  return {info.rcWork.right - size.cx - margin, info.rcWork.top + margin};
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
  const POINT at = info.floating ? top_right(s, size, w.dpi) : centred_on_main(s, size);
  // The Mini window belongs to no other, so that it stays while the main one is minimized or in the tray.
  const DWORD style = info.floating ? WS_POPUP | WS_SYSMENU : WS_CAPTION | WS_SYSMENU | (info.resizable ? WS_THICKFRAME : 0);
  const DWORD ex_style = info.floating ? WS_EX_TOPMOST | WS_EX_TOOLWINDOW : 0;
  // w.hwnd stays empty until this returns: nothing is handled while the window is being made.
  HWND hwnd = CreateWindowExW(ex_style, pane_class, widen(info.title).c_str(), style, at.x, at.y, size.cx, size.cy, info.floating ? nullptr : s.hwnd, nullptr,
                              GetModuleHandleW(nullptr), &w);
  if (!hwnd) return;
  w.hwnd = hwnd;
  w.fitted_w = info.w;
  w.fitted_h = info.h;
  w.dpi = GetDpiForWindow(hwnd);
  SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  set_frame_region(hwnd, w.dpi, region_radius(s, w.pane));
  ShowWindow(hwnd, w.pane == Pane::Offline || w.pane == Pane::Mini ? SW_SHOWNOACTIVATE : SW_SHOW);
}

}  // namespace

void invalidate_panes(Shell& s) {
  for (const PaneWindow& w : s.panes) {
    if (w.hwnd) InvalidateRect(w.hwnd, nullptr, FALSE);
  }
}

void invalidate_mini(Shell& s) {
  if (const HWND hwnd = s.panes[static_cast<std::size_t>(Pane::Mini)].hwnd) InvalidateRect(hwnd, nullptr, FALSE);
}

void drop_pane_targets(Shell& s) {
  for (PaneWindow& w : s.panes) w.target.reset();
}

void recut_pane_regions(Shell& s) {
  for (const PaneWindow& w : s.panes) {
    if (w.hwnd) set_frame_region(w.hwnd, w.dpi, region_radius(s, w.pane));
  }
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
    if (w.hwnd && static_cast<Pane>(i) != Pane::Dialog && static_cast<Pane>(i) != Pane::Mini) EnableWindow(w.hwnd, modal ? FALSE : TRUE);
  }
  for (std::size_t i = 1; i < s.panes.size(); ++i) {
    PaneWindow& w = s.panes[i];
    w.pane = static_cast<Pane>(i);
    if (w.hwnd && !is_open(w.pane)) DestroyWindow(w.hwnd);
  }
  for (std::size_t i = 1; i < s.panes.size(); ++i) {
    PaneWindow& w = s.panes[i];
    // The main window hidden, everything goes but the Mini window, which is for that.
    if (s.hidden && w.pane != Pane::Mini) continue;
    if (!is_open(w.pane)) continue;
    if (!w.hwnd) {
      create_pane(s, w);
      if (w.hwnd && modal && w.pane != Pane::Dialog && w.pane != Pane::Mini) EnableWindow(w.hwnd, FALSE);
    } else if (!IsWindowVisible(w.hwnd)) {
      ShowWindow(w.hwnd, SW_SHOWNOACTIVATE);
    }
    const PaneInfo info = s.app.pane_info(w.pane);
    if (w.hwnd && !info.resizable && (info.w != w.fitted_w || info.h != w.fitted_h)) {
      // Another question in the same window.
      const SIZE size = wanted_size(s, info, w.dpi);
      w.fitted_w = info.w;
      w.fitted_h = info.h;
      RECT was{};
      MONITORINFO screen{sizeof screen};
      if (info.floating && GetWindowRect(w.hwnd, &was) &&
          GetMonitorInfoW(MonitorFromWindow(w.hwnd, MONITOR_DEFAULTTONEAREST), &screen)) {
        // It sits by the screen's right edge from the start, so it grows to the left, and stays on screen.
        const LONG x = std::max(screen.rcWork.left, std::min(was.right, screen.rcWork.right) - size.cx);
        SetWindowPos(w.hwnd, nullptr, x, was.top, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
      } else {
        SetWindowPos(w.hwnd, nullptr, 0, 0, size.cx, size.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
    }
    if (w.hwnd) InvalidateRect(w.hwnd, nullptr, FALSE);
  }
  if (!s.hidden) {
    if (const auto raised = s.app.take_raised_pane()) {
      const PaneWindow& w = s.panes[static_cast<std::size_t>(*raised)];
      // The Mini window comes up without taking the keyboard from what is being worked in.
      if (w.hwnd && *raised != Pane::Mini) SetForegroundWindow(w.hwnd);
    }
  } else {
    for (PaneWindow& w : s.panes) {
      if (w.hwnd && w.pane != Pane::Mini) {
        ShowWindow(w.hwnd, SW_HIDE);
        w.target.reset();
      }
    }
  }
  s.panes_made = s.app.pane_state();
  s.panes_made.hidden = s.hidden;
}

}  // namespace crpp::gui
