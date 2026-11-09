// The window: its messages, rendering target, timers and IME.
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "areas.hpp"
#include "crpp/ui/png.hpp"
#include "crpp/update/built_in.hpp"
#include "os_policy.hpp"
#include "shell.hpp"
#include "tray.hpp"

using namespace crpp::ui;

namespace crpp::gui {

// What the next frame has to draw: only the avatars, when nothing else moves.
void invalidate_frame(Shell& s);

namespace {

// As upstream's tauri.conf.json: the client area in DIPs, and no maximize.
constexpr DWORD window_style = WS_OVERLAPPEDWINDOW & ~static_cast<DWORD>(WS_MAXIMIZEBOX);
constexpr int min_client_width = 517, min_client_height = 450;

void save_window_size(Shell& s, HWND hwnd) {
  RECT rc;
  if (IsIconic(hwnd) || !GetClientRect(hwnd, &rc) || rc.right <= 0 || rc.bottom <= 0) return;
  // Without the scale, so the window keeps its size on any monitor.
  s.store->set("windowWidth", store::Json(static_cast<std::int64_t>(MulDiv(rc.right, 96, static_cast<int>(s.dpi)))));
  s.store->set("windowHeight", store::Json(static_cast<std::int64_t>(MulDiv(rc.bottom, 96, static_cast<int>(s.dpi)))));
}

// What a Skin with a size per screen remembers, under keys of this client's own.
void save_screen_size(Shell& s, HWND hwnd) {
  RECT rc;
  if (s.fitted_screen.empty() || IsIconic(hwnd) || !GetClientRect(hwnd, &rc) || rc.right <= 0 || rc.bottom <= 0) return;
  s.store->set(saved_size_key(s.fitted_screen, "Width"), store::Json(static_cast<std::int64_t>(MulDiv(rc.right, 96, static_cast<int>(s.dpi)))));
  s.store->set(saved_size_key(s.fitted_screen, "Height"), store::Json(static_cast<std::int64_t>(MulDiv(rc.bottom, 96, static_cast<int>(s.dpi)))));
}

void update_region(Shell& s, HWND hwnd) {
  if (s.own_frame) set_frame_region(hwnd, s.dpi, s.app.frame_metrics().radius);
  else SetWindowRgn(hwnd, nullptr, TRUE);
}

// Makes the window the size of what is on screen, if that has changed. The top left stays,
// unless that would leave the window off its monitor.
void fit_window(Shell& s, HWND hwnd, bool save_last = true) {
  const ScreenSize screen = s.app.screen_size();
  if (s.fitted_screen == screen.key) return;
  if (save_last) save_screen_size(s, hwnd);
  s.fitted_screen = std::string(screen.key);
  const auto scaled = [&](std::int64_t dips) { return MulDiv(static_cast<int>(dips), static_cast<int>(s.dpi), 96); };
  RECT rc;
  if (screen.key.empty()) {
    rc = {0, 0, scaled(s.store->i64_or("windowWidth", 951)), scaled(s.store->i64_or("windowHeight", 692))};
    AdjustWindowRectExForDpi(&rc, window_style, FALSE, 0, s.dpi);
  } else {
    const auto side = [&](std::string_view name, float fallback, float least) {
      return scaled(std::max(static_cast<std::int64_t>(least), s.store->i64_or(saved_size_key(screen.key, name), static_cast<std::int64_t>(fallback))));
    };
    rc = {0, 0, side("Width", screen.w, screen.min_w), side("Height", screen.h, screen.min_h)};
  }
  RECT at;
  GetWindowRect(hwnd, &at);
  const LONG w = rc.right - rc.left, h = rc.bottom - rc.top;
  MONITORINFO info{sizeof info};
  LONG x = at.left, y = at.top;
  if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) {
    x = std::max(info.rcWork.left, std::min(x, info.rcWork.right - w));
    y = std::max(info.rcWork.top, std::min(y, info.rcWork.bottom - h));
  }
  SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
}

void ensure_target(Shell& s, HWND hwnd) {
  if (s.target) return;
  RECT rc;
  GetClientRect(hwnd, &rc);
  const auto dpi = static_cast<float>(s.dpi);
  const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi, dpi);
  // Retained, as a frame may draw only part of the window.
  const auto hwnd_props = D2D1::HwndRenderTargetProperties(
      hwnd, D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left), static_cast<UINT32>(rc.bottom - rc.top)),
      D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS);
  if (ID2D1Factory* const factory = s.app.factory()) factory->CreateHwndRenderTarget(props, hwnd_props, s.target.put());
}

}  // namespace

std::string saved_size_key(std::string_view screen, std::string_view side) { return "window" + std::string(screen) + std::string(side); }

// Rounds the top corners; only a region does that on every Windows.
void set_frame_region(HWND hwnd, UINT dpi, float radius_dips) {
  RECT rc;
  if (IsIconic(hwnd) || !GetWindowRect(hwnd, &rc)) return;
  const int w = rc.right - rc.left, h = rc.bottom - rc.top;
  const double radius = static_cast<double>(radius_dips) * dpi / 96;
  // Every pixel the curve touches, row by row: drawing smooths the ones it only partly covers.
  HRGN shape = CreateRectRgn(0, static_cast<int>(std::ceil(radius)), w, h);
  for (int y = 0; y < static_cast<int>(std::ceil(radius)); ++y) {
    const double below = radius - (y + 1);
    const int inset = static_cast<int>(std::floor(radius - std::sqrt(std::max(0.0, radius * radius - below * below))));
    HRGN row = CreateRectRgn(inset, y, w - inset, y + 1);
    CombineRgn(shape, shape, row, RGN_OR);
    DeleteObject(row);
  }
  if (SetWindowRgn(hwnd, shape, TRUE) == 0) DeleteObject(shape);  // otherwise the system owns it
}

LRESULT frame_hit_code(WindowFrame::Zone zone) {
  using Zone = WindowFrame::Zone;
  switch (zone) {
    case Zone::Caption: return HTCAPTION;
    case Zone::Left: return HTLEFT;
    case Zone::Right: return HTRIGHT;
    case Zone::Top: return HTTOP;
    case Zone::Bottom: return HTBOTTOM;
    case Zone::TopLeft: return HTTOPLEFT;
    case Zone::TopRight: return HTTOPRIGHT;
    case Zone::BottomLeft: return HTBOTTOMLEFT;
    case Zone::BottomRight: return HTBOTTOMRIGHT;
    case Zone::Client: break;
  }
  return HTCLIENT;
}

void place_ime(Shell& s, HWND hwnd, UINT dpi) {
  HIMC imc = ImmGetContext(hwnd);
  if (!imc) return;
  const Rect c = s.app.caret_rect();
  const auto pixels = [&](float dips) { return static_cast<LONG>(dips * static_cast<float>(dpi) / 96); };
  COMPOSITIONFORM cf{};
  cf.dwStyle = CFS_POINT;
  cf.ptCurrentPos = {pixels(c.x), pixels(c.y)};
  ImmSetCompositionWindow(imc, &cf);
  CANDIDATEFORM cand{};
  cand.dwStyle = CFS_CANDIDATEPOS;
  cand.ptCurrentPos = {pixels(c.x), pixels(c.bottom())};
  ImmSetCandidateWindow(imc, &cand);
  ImmReleaseContext(hwnd, imc);
}

std::wstring ime_string(HIMC imc, DWORD kind) {
  const LONG bytes = ImmGetCompositionStringW(imc, kind, nullptr, 0);
  if (bytes <= 0) return {};
  std::wstring s(static_cast<std::size_t>(bytes) / sizeof(wchar_t), L'\0');
  ImmGetCompositionStringW(imc, kind, s.data(), static_cast<DWORD>(bytes));
  return s;
}

void complete_composition(HWND hwnd) {
  HIMC imc = ImmGetContext(hwnd);
  if (!imc) return;
  // The IME sends what it had as a result, before the menu opens, as an Edit control's does.
  if (ImmGetCompositionStringW(imc, GCS_COMPSTR, nullptr, 0) > 0) ImmNotifyIME(imc, NI_COMPOSITIONSTR, CPS_COMPLETE, 0);
  ImmReleaseContext(hwnd, imc);
}

namespace {

// The caret timer runs only while a text field can actually take typing.
void sync_blink(Shell& s, HWND hwnd) {
  const bool wanted = s.app.wants_blink() && GetFocus() == hwnd;
  if (wanted == s.blink_timer_on) return;
  if (wanted) SetTimer(hwnd, blink_timer, 530, nullptr);
  else KillTimer(hwnd, blink_timer);
  s.blink_timer_on = wanted;
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* s = reinterpret_cast<Shell*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  if (!s) return DefWindowProcW(hwnd, msg, wp, lp);
  const PaneScope on_main(s->app, Pane::Main);
  if (s->hwnd) {  // not while CreateWindow is still running
    LRESULT result = 0;
    if (area::os::on_message(*s, msg, wp, lp, result)) return result;
  }
  const auto repaint = [&](bool needed, bool only_here = false) {
    if (needed) {
      InvalidateRect(hwnd, nullptr, FALSE);
      if (!only_here) invalidate_panes(*s);
    }
    sync_frames(*s);
  };
  const auto dips = [&](int pixels) { return static_cast<float>(pixels) * 96 / static_cast<float>(s->dpi); };
  switch (msg) {
    case WM_DPICHANGED: {
      s->dpi = HIWORD(wp);
      if (s->target) s->target->SetDpi(static_cast<float>(s->dpi), static_cast<float>(s->dpi));
      const auto* suggested = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                   suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_SETTINGCHANGE:
      forget_cursors(*s);  // the next one takes the pointer's size as it is then
      area::preferences::system_settings_changed(*s);
      return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      sync_blink(*s, hwnd);
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, blink_timer);
      KillTimer(hwnd, frame_timer);
      KillTimer(hwnd, save_timer);
      KillTimer(hwnd, wake_timer);
      KillTimer(hwnd, hold_timer);
      if (s->store) {
        if (s->own_frame) save_screen_size(*s, hwnd);
        else save_window_size(*s, hwnd);
      }
      PostQuitMessage(0);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_NCCALCSIZE:
      // The whole window is client area, and the app draws the frame in it.
      if (s->own_frame && wp) return 0;
      break;
    case WM_NCPAINT:
      if (s->own_frame) return 0;
      break;
    case WM_NCACTIVATE:
      s->app.frame().set_active(wp != FALSE);
      if (s->own_frame) {
        InvalidateRect(hwnd, nullptr, FALSE);
        return DefWindowProcW(hwnd, msg, wp, -1);  // -1: the system draws no frame of its own
      }
      break;
    case WM_NCHITTEST:
      if (s->own_frame) {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);
        return frame_hit_code(WindowFrame::zone_at(dips(pt.x), dips(pt.y), dips(rc.right), dips(rc.bottom), {}, s->app.frame_metrics()));
      }
      break;
    case WM_ACTIVATE:
      area::os::activation(*s, wp, lp);
      if (LOWORD(wp) == WA_INACTIVE) {
        if (s->own_frame) {
          repaint(s->app.window_deactivated(), true);
        } else if (s->app.context_menu().is_open()) {  // a menu goes with the keyboard
          s->app.context_menu().close();
          repaint(true, true);
        }
      }
      break;
    case wm_sync_panes:
      sync_panes(*s);
      return 0;
    case WM_EXITSIZEMOVE:
      if (s->own_frame && s->store) {
        save_screen_size(*s, hwnd);
        if (!s->save_timer_on) {
          SetTimer(hwnd, save_timer, save_delay_ms, nullptr);
          s->save_timer_on = true;
        }
      }
      break;
    case wm_exit:
      DestroyWindow(hwnd);
      return 0;
    case WM_GETMINMAXINFO: {
      if (s->own_frame) {
        const ScreenSize screen = s->app.screen_size();
        reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {MulDiv(static_cast<int>(screen.min_w), static_cast<int>(s->dpi), 96),
                                                             MulDiv(static_cast<int>(screen.min_h), static_cast<int>(s->dpi), 96)};
        return 0;
      }
      RECT rc{0, 0, MulDiv(min_client_width, static_cast<int>(s->dpi), 96), MulDiv(min_client_height, static_cast<int>(s->dpi), 96)};
      AdjustWindowRectExForDpi(&rc, window_style, FALSE, 0, s->dpi);
      reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {rc.right - rc.left, rc.bottom - rc.top};
      return 0;
    }
    case WM_SIZE:
      if (s->target) s->target->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
      if (s->own_frame && wp != SIZE_MINIMIZED) update_region(*s, hwnd);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_PAINT: {
      // Before anything is drawn, so a screen never shows at another's size.
      if (s->store) fit_window(*s, hwnd);
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      if (s->frame_running) s->app.tick(clock_ms());
      const bool new_target = !s->target;
      ensure_target(*s, hwnd);
      if (s->target) {
        const D2D1_SIZE_F size = s->target->GetSize();  // in DIPs
        s->target->BeginDraw();
        // The whole frame is walked, as hits and layout come from painting, but the GPU only
        // touches what asked to be drawn. The rest is still there from the last frame.
        const bool part = !new_target && !s->app.wants_full_frame() && (ps.rcPaint.right - ps.rcPaint.left < static_cast<LONG>(size.width * static_cast<float>(s->dpi) / 96) ||
                                          ps.rcPaint.bottom - ps.rcPaint.top < static_cast<LONG>(size.height * static_cast<float>(s->dpi) / 96));
        if (part) {
          const auto to_dips = [&](LONG pixels) { return static_cast<float>(pixels) * 96 / static_cast<float>(s->dpi); };
          s->target->SetTransform(D2D1::Matrix3x2F::Identity());
          s->target->PushAxisAlignedClip(D2D1::RectF(to_dips(ps.rcPaint.left), to_dips(ps.rcPaint.top), to_dips(ps.rcPaint.right), to_dips(ps.rcPaint.bottom)),
                                         D2D1_ANTIALIAS_MODE_ALIASED);
        }
        s->app.paint(s->target.get(), size.width, size.height);
        if (part) s->target->PopAxisAlignedClip();
        if (s->target->EndDraw() == D2DERR_RECREATE_TARGET) {
          s->target.reset();
          s->app.ui().forget_render_target();
        }
      }
      EndPaint(hwnd, &ps);
      place_ime(*s, hwnd, s->dpi);
      s->last_frame_ms = clock_ms();
      sync_frames(*s);  // painting is where a hover's transition starts
      if (!s->frame_running) area::os::painted(*s);
      // A window that keeps painting would never see its timers: they come after paint.
      for (MSG timer; PeekMessageW(&timer, hwnd, WM_TIMER, WM_TIMER, PM_REMOVE);) DispatchMessageW(&timer);
      return 0;
    }
    case wm_ui_task:
      if (s->tasks.run_all()) {
        InvalidateRect(hwnd, nullptr, FALSE);
        invalidate_panes(*s);
      }
      if (s->app.room().take_new_pulse()) invalidate_frame(*s);
      sync_blink(*s, hwnd);
      sync_frames(*s);
      if (s->store && s->store->has_unsaved() && !s->save_timer_on) {
        SetTimer(hwnd, save_timer, save_delay_ms, nullptr);
        s->save_timer_on = true;
      }
      return 0;
    case wm_own_input:
      s->input_sink.pulse_taken();
      area::sound::own_input(*s);
      s->app.room().pulse_me();
      s->app.note_own_input(s->input_sink.take_events(), s->input_sink.take_presses(), s->input_sink.take_clicks());
      InvalidateRect(hwnd, nullptr, FALSE);
      sync_frames(*s);
      return 0;
    case WM_MOUSEMOVE:
      if (!s->tracking) {
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&t);
        s->tracking = true;
      }
      repaint(s->app.mouse_move(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp))), true);
      return 0;
    case WM_MOUSELEAVE:
      s->tracking = false;
      repaint(s->app.mouse_leave(), true);
      return 0;
    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      SetCapture(hwnd);
      repaint(s->app.mouse_down(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp)), (wp & MK_SHIFT) != 0));
      if (const unsigned wait = s->app.hold_wait_ms()) SetTimer(hwnd, hold_timer, wait, nullptr);
      sync_blink(*s, hwnd);
      return 0;
    case WM_LBUTTONUP: {
      // Before the capture goes, since losing it calls off a press.
      const bool changed = s->app.mouse_up(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp)));
      ReleaseCapture();
      repaint(changed);
      sync_blink(*s, hwnd);
      return 0;
    }
    case WM_CAPTURECHANGED:
      KillTimer(hwnd, hold_timer);
      repaint(s->app.mouse_cancel());
      return 0;
    case WM_MOUSEWHEEL: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &pt);
      repaint(s->app.wheel(dips(pt.x), dips(pt.y), static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA), true);
      return 0;
    }
    case WM_RBUTTONUP:
      // As Windows does, on the way up; what is being composed goes in first.
      complete_composition(hwnd);
      repaint(s->app.right_click(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp))));
      return 0;
    case WM_SETCURSOR:
      if (apply_cursor(*s, LOWORD(lp), s->dpi)) return TRUE;
      break;
    case WM_CHAR:
      repaint(s->app.char_input(static_cast<wchar_t>(wp), (GetKeyState(VK_SHIFT) & 0x8000) != 0));
      return 0;
    case WM_SYSKEYDOWN:
      if (wp == VK_F10 && (GetKeyState(VK_SHIFT) & 0x8000) != 0) complete_composition(hwnd);
      if (s->app.sys_key(static_cast<unsigned>(wp), (GetKeyState(VK_SHIFT) & 0x8000) != 0)) {
        repaint(true);
        return 0;
      }
      break;
    case WM_SYSKEYUP:
      if (s->app.sys_key_up(static_cast<unsigned>(wp))) {
        repaint(true);
        return 0;
      }
      break;
    case WM_SYSCHAR:
      // Alt+Space and the rest still reach the system's own menu.
      if (s->app.take_sys_char(static_cast<wchar_t>(wp))) return 0;
      break;
    case WM_KEYDOWN:
      if (wp == VK_APPS) complete_composition(hwnd);
      repaint(s->app.key_down(static_cast<unsigned>(wp), (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                              (GetKeyState(VK_SHIFT) & 0x8000) != 0));
      return 0;
    case WM_IME_STARTCOMPOSITION:
      place_ime(*s, hwnd, s->dpi);
      return 0;  // our own drawing replaces the IME's composition window
    case WM_IME_COMPOSITION: {
      HIMC imc = ImmGetContext(hwnd);
      std::wstring result, comp;
      if (imc) {
        if (lp & GCS_RESULTSTR) result = ime_string(imc, GCS_RESULTSTR);
        if (lp & GCS_COMPSTR) comp = ime_string(imc, GCS_COMPSTR);
        ImmReleaseContext(hwnd, imc);
      }
      repaint(s->app.ime_composition(comp, result));
      return 0;
    }
    case WM_IME_ENDCOMPOSITION:
      repaint(s->app.ime_composition({}, {}));
      return 0;
    case WM_TIMER:
      if (wp == smoke_timer) {
        const ULONGLONG elapsed = clock_ms() - s->smoke_start;
        if (!s->smoke_left && s->app.view() == View::Session && s->app.room().member_count() > 0) {
          RECT rc;
          GetClientRect(hwnd, &rc);
          const bool saved = save_png(s->app, s->smoke_path, static_cast<unsigned>(dips(rc.right)),
                                      static_cast<unsigned>(dips(rc.bottom)), static_cast<float>(s->dpi));
          s->smoke_left = true;
          s->client->leave();
          ExitProcess(saved ? 0 : 1);
        } else if (elapsed > 20000) {
          ExitProcess(1);
        }
      } else if (wp == save_timer) {
        KillTimer(hwnd, save_timer);
        s->save_timer_on = false;
        area::achievements::flush(*s);
        (void)s->store->save();
      } else if (wp == blink_timer) {
        repaint(s->app.blink(), true);
      } else if (wp == hold_timer) {
        SetTimer(hwnd, hold_timer, s->app.hold_repeat_ms(), nullptr);
        repaint(s->app.hold_step());
      } else if (wp == wake_timer) {
        KillTimer(hwnd, wake_timer);
        if (s->app.tick(clock_ms())) {
          invalidate_frame(*s);
          invalidate_panes(*s);
        }
        sync_frames(*s);
      } else if (wp == frame_timer) {
        KillTimer(hwnd, frame_timer);
        s->frame_timer_on = false;
        invalidate_frame(*s);
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

void frame_changed(Shell& s) {
  const bool own = s.app.own_frame();
  const bool frame_differs = own != s.own_frame;
  if (frame_differs) {
    if (own) save_window_size(s, s.hwnd);
    else save_screen_size(s, s.hwnd);
    s.own_frame = own;
  }
  // A Theme of the same frame may round its corners by another radius, at the same size.
  update_region(s, s.hwnd);
  recut_pane_regions(s);
  if (frame_differs) {
    // The frame is measured again now, which WM_NCCALCSIZE answers.
    SetWindowPos(s.hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
  fit_window(s, s.hwnd, !frame_differs);  // saved already, and the client area is another now
  invalidate_panes(s);
  request_pane_sync(s);
}

void invalidate_frame(Shell& s) {
  if (const auto area = s.app.only_moving_area()) {
    const auto pixels = [&](float dips, double round) {
      return static_cast<LONG>(std::floor(static_cast<double>(dips) * s.dpi / 96 + round));
    };
    const RECT rc{pixels(area->x, 0), pixels(area->y, 0), pixels(area->right(), 1), pixels(area->bottom(), 1)};
    InvalidateRect(s.hwnd, &rc, FALSE);
  } else {
    InvalidateRect(s.hwnd, nullptr, FALSE);
  }
}

void sync_frames(Shell& s) {
  const PaneScope on_main(s.app, Pane::Main);
  request_pane_sync(s);
  // While something moves, each frame asks for the next. Presenting waits for the display,
  // so that is its pace, and nothing runs once it all stops. The floor is for a target that
  // does not wait, as on a hidden desktop.
  constexpr unsigned long long shortest_frame_ms = 4;
  if (s.hidden) {  // nobody to animate for; showing the window calls this again
    if (s.frame_timer_on) KillTimer(s.hwnd, frame_timer);
    s.frame_timer_on = false;
    s.frame_running = false;
  } else if (s.app.animating()) {
    s.frame_running = true;
    if (clock_ms() - s.last_frame_ms >= shortest_frame_ms) {
      invalidate_frame(s);
    } else if (!s.frame_timer_on) {
      SetTimer(s.hwnd, frame_timer, static_cast<UINT>(shortest_frame_ms), nullptr);
      s.frame_timer_on = true;
    }
  } else if (s.frame_running) {
    s.frame_running = false;
    if (s.app.tick(clock_ms())) invalidate_frame(s);  // the last frame shows the end state
  }
  if (const auto at = s.app.wake_at()) {
    const auto now = clock_ms();
    SetTimer(s.hwnd, wake_timer, at > now ? static_cast<UINT>(at - now) : 1, nullptr);
  } else {
    KillTimer(s.hwnd, wake_timer);
  }
}

HWND create_window(Shell& s) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hIcon = load_app_icon(GetSystemMetrics(SM_CXICON));
  wc.hIconSm = load_app_icon(GetSystemMetrics(SM_CXSMICON));
  const bool official = update::official_build();
  wc.lpszClassName = os_policy::window_class(official);
  RegisterClassExW(&wc);

  // The monitor's DPI is known only once the window exists, so it is sized while
  // still hidden.
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, os_policy::window_title(official), window_style, CW_USEDEFAULT, CW_USEDEFAULT,
                              CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, wc.hInstance, &s);
  if (!hwnd) return nullptr;
  s.dpi = GetDpiForWindow(hwnd);
  const auto scaled = [&](std::int64_t dips) { return MulDiv(static_cast<int>(dips), static_cast<int>(s.dpi), 96); };
  RECT rc{0, 0, scaled(std::clamp<std::int64_t>(s.store->i64_or("windowWidth", 951), min_client_width, 16384)),
          scaled(std::clamp<std::int64_t>(s.store->i64_or("windowHeight", 692), min_client_height, 16384))};
  AdjustWindowRectExForDpi(&rc, window_style, FALSE, 0, s.dpi);
  SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  return hwnd;
}

}  // namespace crpp::gui
