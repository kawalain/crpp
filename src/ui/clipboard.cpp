#include "crpp/ui/clipboard.hpp"

#include <windows.h>

#include <algorithm>

namespace crpp::ui {

bool SystemClipboard::set_text(std::wstring_view text) {
  if (!OpenClipboard(nullptr)) return false;
  bool copied = false;
  if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t))) {
    if (auto* dst = static_cast<wchar_t*>(GlobalLock(mem))) {
      std::ranges::copy(text, dst);
      dst[text.size()] = L'\0';
      GlobalUnlock(mem);
      EmptyClipboard();
      copied = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
    }
    if (!copied) GlobalFree(mem);  // the system owns it only once it is set
  }
  CloseClipboard();
  return copied;
}

std::optional<std::wstring> SystemClipboard::text() {
  if (!OpenClipboard(nullptr)) return std::nullopt;
  std::optional<std::wstring> result;
  if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
    if (const auto* s = static_cast<const wchar_t*>(GlobalLock(h))) {
      // Not trusted to end where it should: never past the block it came in.
      const std::size_t room = GlobalSize(h) / sizeof(wchar_t);
      result.emplace(s, std::find(s, s + room, L'\0'));
      GlobalUnlock(h);
    }
  }
  CloseClipboard();
  return result;
}

bool SystemClipboard::has_text() { return IsClipboardFormatAvailable(CF_UNICODETEXT) != FALSE; }

}  // namespace crpp::ui
