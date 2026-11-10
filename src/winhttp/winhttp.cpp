#include "crpp/winhttp/winhttp.hpp"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>

namespace crpp::winhttp {

namespace {

// Small reads, so that progress moves while a large file arrives.
constexpr std::size_t read_size = 64 * 1024;

}  // namespace

void Handle::reset(Raw handle) noexcept {
  if (handle_ != nullptr) WinHttpCloseHandle(handle_);
  handle_ = handle;
}

std::wstring widen(std::string_view utf8) {
  if (utf8.empty()) return {};
  const int size = static_cast<int>(utf8.size());
  std::wstring wide(static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, nullptr, 0)), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), size, wide.data(), static_cast<int>(wide.size()));
  return wide;
}

bool header_safe(std::string_view value) noexcept {
  return std::ranges::all_of(value, [](char c) {
    const auto u = static_cast<unsigned char>(c);
    return u >= 0x20 && u != 0x7F;
  });
}

Handle open_session(const wchar_t* agent, int timeout_ms, Redirects redirects) {
  Handle session(WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if (!session) return {};
  WinHttpSetTimeouts(session.get(), timeout_ms, timeout_ms, timeout_ms, timeout_ms);
  if (redirects == Redirects::Never) {
    DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(session.get(), WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof never)) return {};
  }
  return session;
}

std::optional<Address> crack(std::string_view url) {
  const std::wstring wide(url.begin(), url.end());  // ASCII, as every address this is given
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof parts;
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  parts.dwExtraInfoLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) return std::nullopt;
  // A part the address does not have may come back as a null pointer.
  const auto part = [](const wchar_t* text, DWORD length) {
    return text ? std::wstring_view(text, length) : std::wstring_view{};
  };
  Address address;
  address.host = part(parts.lpszHostName, parts.dwHostNameLength);
  address.path = std::wstring(part(parts.lpszUrlPath, parts.dwUrlPathLength));
  address.path += part(parts.lpszExtraInfo, parts.dwExtraInfoLength);
  address.port = parts.nPort;
  address.secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
  return address;
}

std::optional<unsigned long> status_of(Raw request) {
  DWORD status = 0;
  DWORD size = sizeof status;
  if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                           WINHTTP_NO_HEADER_INDEX)) {
    return std::nullopt;
  }
  return status;
}

bool read_body(Raw request, std::size_t limit, std::vector<std::uint8_t>& out, const std::move_only_function<void(std::uint64_t) const>& on_bytes) {
  // One byte of room beyond the limit is enough to tell that it is too long.
  const std::size_t room = limit + 1;
  if (out.empty()) out.reserve(std::min<std::size_t>(room, 16 * 1024 * 1024));
  // Straight into the end of `out`, not through a 64 KB buffer on the thread's stack.
  while (out.size() < room) {
    const std::size_t at = out.size();
    const std::size_t want = std::min(read_size, room - at);
    out.resize(at + want);
    DWORD read = 0;
    const bool ok = WinHttpReadData(request, out.data() + at, static_cast<DWORD>(want), &read) != FALSE;
    out.resize(at + (ok ? std::min<std::size_t>(read, want) : 0));
    if (!ok) return false;
    if (read == 0) break;
    if (on_bytes) on_bytes(out.size());
  }
  return true;
}

}  // namespace crpp::winhttp
