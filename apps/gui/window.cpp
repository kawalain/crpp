// The window: its messages, rendering target, timers and IME.
#include <windowsx.h>

#include <algorithm>
#include <cstdio>

#include "areas.hpp"
#include "crpp/ui/png.hpp"
#include "shell.hpp"

using namespace crpp::ui;

namespace crpp::gui {

namespace {

void save_window_size(Shell& s, HWND hwnd) {
  RECT rc;
  if (IsIconic(hwnd) || !GetClientRect(hwnd, &rc) || rc.right <= 0 || rc.bottom <= 0) return;
  // Without the scale, so the window keeps its size on any monitor.
  s.store->set("windowWidth", store::Json(static_cast<std::int64_t>(MulDiv(rc.right, 96, static_cast<int>(s.dpi)))));
  s.store->set("windowHeight", store::Json(static_cast<std::int64_t>(MulDiv(rc.bottom, 96, static_cast<int>(s.dpi)))));
}

void ensure_target(Shell& s, HWND hwnd) {
  if (s.target) return;
  RECT rc;
  GetClientRect(hwnd, &rc);
  const auto dpi = static_cast<float>(s.dpi);
  const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), dpi, dpi);
  const auto hwnd_props = D2D1::HwndRenderTargetProperties(
      hwnd, D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left), static_cast<UINT32>(rc.bottom - rc.top)));
  s.app.factory()->CreateHwndRenderTarget(props, hwnd_props, s.target.put());
}

void place_ime(Shell& s, HWND hwnd) {
  HIMC imc = ImmGetContext(hwnd);
  if (!imc) return;
  const Rect c = s.app.caret_rect();
  const auto pixels = [&](float dips) { return static_cast<LONG>(dips * static_cast<float>(s.dpi) / 96); };
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
  if (s->hwnd) {  // not while CreateWindow is still running
    LRESULT result = 0;
    if (area::os::on_message(*s, msg, wp, lp, result)) return result;
  }
  const auto repaint = [&](bool needed) {
    if (needed) InvalidateRect(hwnd, nullptr, FALSE);
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
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      sync_blink(*s, hwnd);
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, blink_timer);
      KillTimer(hwnd, frame_timer);
      KillTimer(hwnd, save_timer);
      if (s->store) save_window_size(*s, hwnd);
      PostQuitMessage(0);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_SIZE:
      if (s->target) s->target->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(hwnd, &ps);
      ensure_target(*s, hwnd);
      if (s->target) {
        const D2D1_SIZE_F size = s->target->GetSize();  // in DIPs
        s->target->BeginDraw();
        s->app.paint(s->target.get(), size.width, size.height);
        if (s->target->EndDraw() == D2DERR_RECREATE_TARGET) s->target.reset();
      }
      EndPaint(hwnd, &ps);
      place_ime(*s, hwnd);
      return 0;
    }
    case wm_ui_task:
      s->tasks.run_all();
      InvalidateRect(hwnd, nullptr, FALSE);
      sync_blink(*s, hwnd);
      sync_frames(*s);
      if (s->store && s->store->has_unsaved() && !s->save_timer_on) {
        SetTimer(hwnd, save_timer, save_delay_ms, nullptr);
        s->save_timer_on = true;
      }
      return 0;
    case wm_own_input:
      s->input_sink.pulse_taken();
      s->app.room().pulse_me();
      InvalidateRect(hwnd, nullptr, FALSE);
      sync_frames(*s);
      return 0;
    case WM_MOUSEMOVE:
      if (!s->tracking) {
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&t);
        s->tracking = true;
      }
      repaint(s->app.mouse_move(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp))));
      return 0;
    case WM_MOUSELEAVE:
      s->tracking = false;
      repaint(s->app.mouse_leave());
      return 0;
    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      SetCapture(hwnd);
      repaint(s->app.mouse_down(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp)), (wp & MK_SHIFT) != 0));
      sync_blink(*s, hwnd);
      return 0;
    case WM_LBUTTONUP:
      ReleaseCapture();
      repaint(s->app.mouse_up());
      return 0;
    case WM_MOUSEWHEEL: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      ScreenToClient(hwnd, &pt);
      repaint(s->app.wheel(dips(pt.x), dips(pt.y), static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA));
      return 0;
    }
    case WM_RBUTTONDOWN:
      repaint(s->app.right_click(dips(GET_X_LPARAM(lp)), dips(GET_Y_LPARAM(lp))));
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        const Cursor c = s->app.cursor();
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(c == Cursor::Hand ? 32649 : c == Cursor::IBeam ? 32513 : 32512)));
        return TRUE;
      }
      break;
    case WM_CHAR:
      repaint(s->app.char_input(static_cast<wchar_t>(wp), (GetKeyState(VK_SHIFT) & 0x8000) != 0));
      return 0;
    case WM_KEYDOWN:
      repaint(s->app.key_down(static_cast<unsigned>(wp), (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                              (GetKeyState(VK_SHIFT) & 0x8000) != 0));
      return 0;
    case WM_IME_STARTCOMPOSITION:
      place_ime(*s, hwnd);
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
        const ULONGLONG elapsed = GetTickCount64() - s->smoke_start;
        if (!s->smoke_left && s->app.view() == View::Session && s->app.room().member_count() > 0) {
          RECT rc;
          GetClientRect(hwnd, &rc);
          const bool saved = save_png(s->app, s->smoke_path, static_cast<unsigned>(dips(rc.right)),
                                      static_cast<unsigned>(dips(rc.bottom)), static_cast<float>(s->dpi));
          std::printf("smoke: in room, screenshot %s\n", saved ? "saved" : "FAILED");
          s->smoke_left = true;
          s->client->leave();
          ExitProcess(saved ? 0 : 1);
        } else if (elapsed > 20000) {
          std::printf("smoke: timed out before entering a room\n");
          ExitProcess(1);
        }
      } else if (wp == save_timer) {
        KillTimer(hwnd, save_timer);
        s->save_timer_on = false;
        (void)s->store->save();
      } else if (wp == blink_timer) {
        repaint(s->app.blink());
      } else if (wp == frame_timer) {
        const bool changed = s->app.tick(GetTickCount64());
        repaint(changed);
        if (!s->app.animating()) {
          KillTimer(hwnd, frame_timer);
          s->frame_timer_on = false;
        }
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

void sync_frames(Shell& s) {
  if (s.app.animating() && !s.frame_timer_on) {
    SetTimer(s.hwnd, frame_timer, 16, nullptr);
    s.frame_timer_on = true;
  }
}

HWND create_window(Shell& s) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.lpszClassName = L"CrppWindow";
  RegisterClassExW(&wc);

  // The monitor's DPI is known only once the window exists, so it is sized while
  // still hidden.
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Cloudroom", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, wc.hInstance, &s);
  if (!hwnd) return nullptr;
  s.dpi = GetDpiForWindow(hwnd);
  const auto scaled = [&](std::int64_t dips) { return MulDiv(static_cast<int>(dips), static_cast<int>(s.dpi), 96); };
  RECT rc{0, 0, scaled(std::clamp<std::int64_t>(s.store->i64_or("windowWidth", 951), 480, 16384)),
          scaled(std::clamp<std::int64_t>(s.store->i64_or("windowHeight", 692), 360, 16384))};
  AdjustWindowRectExForDpi(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0, s.dpi);
  SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  return hwnd;
}

}  // namespace crpp::gui
