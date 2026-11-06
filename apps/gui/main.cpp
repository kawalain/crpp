// crpp: the native Cloudroom window.
//   crpp                      run the app
//   crpp --shot out.png [WxH] render the home screen offscreen and exit
#include <windows.h>
#include <windowsx.h>

#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "crpp/ui/app.hpp"
#include "crpp/ui/png.hpp"

using namespace crpp::ui;

namespace {

constexpr UINT_PTR blink_timer = 1;

struct Shell {
  App app;
  ComPtr<ID2D1HwndRenderTarget> target;
  bool tracking = false;
};

void ensure_target(Shell& s, HWND hwnd) {
  if (s.target) return;
  RECT rc;
  GetClientRect(hwnd, &rc);
  const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), 96, 96);
  const auto hwnd_props = D2D1::HwndRenderTargetProperties(
      hwnd, D2D1::SizeU(static_cast<UINT32>(rc.right - rc.left), static_cast<UINT32>(rc.bottom - rc.top)));
  s.app.factory()->CreateHwndRenderTarget(props, hwnd_props, s.target.put());
}

void place_ime(Shell& s, HWND hwnd) {
  HIMC imc = ImmGetContext(hwnd);
  if (!imc) return;
  const Rect c = s.app.caret_rect();
  COMPOSITIONFORM cf{CFS_POINT, {static_cast<LONG>(c.x), static_cast<LONG>(c.y)}, {}};
  ImmSetCompositionWindow(imc, &cf);
  CANDIDATEFORM cand{0, CFS_CANDIDATEPOS, {static_cast<LONG>(c.x), static_cast<LONG>(c.bottom())}, {}};
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

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  auto* s = reinterpret_cast<Shell*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  if (!s) return DefWindowProcW(hwnd, msg, wp, lp);
  const auto repaint = [&](bool needed) {
    if (needed) InvalidateRect(hwnd, nullptr, FALSE);
  };
  switch (msg) {
    case WM_CREATE:
      SetTimer(hwnd, blink_timer, 530, nullptr);
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, blink_timer);
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
        RECT rc;
        GetClientRect(hwnd, &rc);
        s->target->BeginDraw();
        s->app.paint(s->target.get(), static_cast<float>(rc.right), static_cast<float>(rc.bottom));
        if (s->target->EndDraw() == D2DERR_RECREATE_TARGET) s->target.reset();
      }
      EndPaint(hwnd, &ps);
      place_ime(*s, hwnd);
      return 0;
    }
    case WM_MOUSEMOVE:
      if (!s->tracking) {
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&t);
        s->tracking = true;
      }
      repaint(s->app.mouse_move(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp))));
      return 0;
    case WM_MOUSELEAVE:
      s->tracking = false;
      repaint(s->app.mouse_leave());
      return 0;
    case WM_LBUTTONDOWN:
      SetFocus(hwnd);
      repaint(s->app.mouse_down(static_cast<float>(GET_X_LPARAM(lp)), static_cast<float>(GET_Y_LPARAM(lp)),
                                (wp & MK_SHIFT) != 0));
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        const Cursor c = s->app.cursor();
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(c == Cursor::Hand ? 32649 : c == Cursor::IBeam ? 32513 : 32512)));
        return TRUE;
      }
      break;
    case WM_CHAR:
      repaint(s->app.char_input(static_cast<wchar_t>(wp)));
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
      if (wp == blink_timer) repaint(s->app.blink());
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void configure(App& app) {
  app.set_nickname("크롬이");
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  app.set_hour(local.tm_hour);
}

}  // namespace

int main(int argc, char** argv) {
  SetProcessDPIAware();
  Shell shell;
  if (!shell.app.init()) {
    std::fprintf(stderr, "failed to initialise Direct2D\n");
    return 1;
  }
  configure(shell.app);

  if (argc >= 3 && std::string(argv[1]) == "--shot") {
    unsigned w = 951, h = 692;
    if (argc >= 4) std::sscanf(argv[3], "%ux%u", &w, &h);
    if (argc >= 5) shell.app.set_hour(std::atoi(argv[4]));
    const std::wstring path = widen(argv[2]);
    return save_png(shell.app, path, w, h) ? 0 : 1;
  }

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.lpszClassName = L"CrppWindow";
  RegisterClassExW(&wc);

  RECT rc{0, 0, 951, 692};
  AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
  HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Cloudroom", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                              CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance,
                              &shell);
  if (!hwnd) return 1;
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return 0;
}
