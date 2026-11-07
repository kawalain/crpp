// The pointer in the app's windows: the pack's cursors, made on first use for this size, or the system's.
#include "crpp/ui/cursor_image.hpp"
#include "shell.hpp"

namespace crpp::gui {

namespace {

using skin::CursorKind;

// No Microsoft page says what follows the "Pointer size" setting. Qt reads this value, which does
// not depend on the DPI, and so does this; without it a cursor is 32 at 100%.
int pointer_size_at(UINT dpi) {
  DWORD base = 0, length = sizeof base;
  if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Cursors", L"CursorBaseSize", RRF_RT_REG_DWORD, nullptr, &base, &length) != ERROR_SUCCESS || base < 32 || base > 256) base = 32;
  return std::min(256, MulDiv(static_cast<int>(base), static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI));
}

HCURSOR made_cursor(Shell& s, CursorKind kind, UINT dpi) {
  const int size = pointer_size_at(dpi);
  for (const MadeCursor& made : s.cursors) {
    if (made.kind == kind && made.size == size) return made.cursor;
  }
  const auto image = ui::make_cursor_image(s.app.ui(), kind, size);
  HCURSOR cursor = image ? ui::make_system_cursor(*image) : nullptr;
  if (cursor) s.cursors.push_back({kind, size, cursor});
  return cursor;
}

CursorKind kind_of(ui::Cursor cursor) {
  switch (cursor) {
    case ui::Cursor::Hand: return CursorKind::Hand;
    case ui::Cursor::IBeam: return CursorKind::IBeam;
    default: return CursorKind::Arrow;
  }
}

CursorKind kind_at_edge(UINT hit) {
  switch (hit) {
    case HTLEFT:
    case HTRIGHT: return CursorKind::SizeWE;
    case HTTOP:
    case HTBOTTOM: return CursorKind::SizeNS;
    case HTTOPLEFT:
    case HTBOTTOMRIGHT: return CursorKind::SizeNWSE;
    case HTTOPRIGHT:
    case HTBOTTOMLEFT: return CursorKind::SizeNESW;
    default: return CursorKind::Arrow;
  }
}

}  // namespace

bool apply_cursor(Shell& s, UINT hit, UINT dpi) {
  if (s.app.theme_cursors()) {
    const CursorKind kind = hit == HTCLIENT ? kind_of(s.app.cursor()) : kind_at_edge(hit);
    if (HCURSOR cursor = made_cursor(s, kind, dpi)) {
      SetCursor(cursor);
      return true;
    }
  }
  if (hit != HTCLIENT) return false;
  const ui::Cursor c = s.app.cursor();
  SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(c == ui::Cursor::Hand ? 32649 : c == ui::Cursor::IBeam ? 32513 : 32512)));
  return true;
}

void reapply_cursor(Shell& s) {
  (void)s;
  POINT at;
  if (!GetCursorPos(&at)) return;
  HWND under = WindowFromPoint(at);
  if (!under || GetWindowThreadProcessId(under, nullptr) != GetCurrentThreadId()) return;
  const auto hit = static_cast<UINT>(SendMessageW(under, WM_NCHITTEST, 0, MAKELPARAM(at.x, at.y)));
  SendMessageW(under, WM_SETCURSOR, reinterpret_cast<WPARAM>(under), MAKELPARAM(hit, WM_MOUSEMOVE));
}

void forget_cursors(Shell& s) {
  if (s.cursors.empty()) return;
  // One in use cannot be destroyed.
  SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
  for (const MadeCursor& made : s.cursors) DestroyCursor(made.cursor);
  s.cursors.clear();
}

}  // namespace crpp::gui
