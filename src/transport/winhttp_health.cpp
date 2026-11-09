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
#include <vector>

#include "crpp/winhttp/winhttp.hpp"

namespace crpp::transport {

using winhttp::widen;

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
    const int ms = static_cast<int>(timeout_.count());
    // A server that sends this elsewhere is not the one that was asked about.
    if (!publish(session_, winhttp::open_session(L"crpp", ms, winhttp::Redirects::Never).release())) return std::nullopt;
    const std::wstring host = widen(target.host), path = widen(target.path_and_query);
    Handle request = nullptr;
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
    const auto status = winhttp::status_of(request);
    if (!status || *status < 200 || *status >= 300) return std::nullopt;
    // Upstream answers a few short lines; anything past a KB is not read. What arrived before a
    // failure is still an answer, as the status was.
    std::vector<std::uint8_t> bytes;
    (void)winhttp::read_body(request, 1023, bytes);
    return std::string(bytes.begin(), bytes.end());
  };
  std::optional<std::string> answer = ask();
  release();
  return answer;
}

}  // namespace crpp::transport
