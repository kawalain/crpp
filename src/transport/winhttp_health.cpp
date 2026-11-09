#include "crpp/transport/winhttp_health.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <string>
#include <string_view>

namespace crpp::transport {

namespace {

std::wstring widen(std::string_view s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

}  // namespace

bool WinHttpHealth::publish(Handle& slot, Handle handle) {
  std::lock_guard lock(mutex_);
  if (cancelled_) {
    if (handle != nullptr) WinHttpCloseHandle(handle);
    return false;
  }
  slot = handle;
  return handle != nullptr;
}

// Children before parents. Closing wakes a blocked call on another thread, as the
// WebSocket transport's tests pin.
void WinHttpHealth::release() {
  std::lock_guard lock(mutex_);
  for (Handle* slot : {&request_, &connection_, &session_}) {
    if (*slot != nullptr) WinHttpCloseHandle(*slot);
    *slot = nullptr;
  }
}

void WinHttpHealth::cancel() {
  {
    std::lock_guard lock(mutex_);
    cancelled_ = true;
  }
  release();
}

std::optional<std::string> WinHttpHealth::ask(const Target& target) {
  const auto ask = [&]() -> std::optional<std::string> {
    if (!publish(session_, WinHttpOpen(L"crpp", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                       WINHTTP_NO_PROXY_BYPASS, 0))) {
      return std::nullopt;
    }
    const int ms = static_cast<int>(timeout_.count());
    const std::wstring host = widen(target.host), path = widen(target.path_and_query);
    Handle request = nullptr;
    {
      std::lock_guard lock(mutex_);
      if (session_ == nullptr) return std::nullopt;
      WinHttpSetTimeouts(session_, ms, ms, ms, ms);
    }
    Handle connection = nullptr;
    {
      std::lock_guard lock(mutex_);
      connection = session_ != nullptr ? WinHttpConnect(session_, host.c_str(), target.port, 0) : nullptr;
    }
    if (!publish(connection_, connection)) return std::nullopt;
    {
      std::lock_guard lock(mutex_);
      request = connection_ != nullptr
                    ? WinHttpOpenRequest(connection_, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, target.secure ? WINHTTP_FLAG_SECURE : 0)
                    : nullptr;
    }
    if (!publish(request_, request)) return std::nullopt;
    // Blocking calls run without the lock, so cancel() can close the handle under them.
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) return std::nullopt;
    if (!WinHttpReceiveResponse(request, nullptr)) return std::nullopt;
    DWORD status = 0, size = sizeof status;
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &status, &size, WINHTTP_NO_HEADER_INDEX) ||
        status < 200 || status >= 300) {
      return std::nullopt;
    }
    // Upstream answers a few short lines; anything past a KB is not read.
    std::string body;
    char buffer[256];
    DWORD read = 0;
    while (body.size() < 1024 && WinHttpReadData(request, buffer, sizeof buffer, &read) && read > 0) body.append(buffer, read);
    return body;
  };
  std::optional<std::string> answer = ask();
  release();
  return answer;
}

}  // namespace crpp::transport
