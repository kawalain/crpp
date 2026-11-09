#include "crpp/transport/winhttp_transport.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

namespace crpp::transport {

namespace {

std::wstring widen(std::string_view s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

HINTERNET as_handle(void* h) { return static_cast<HINTERNET>(h); }

// A header value must not be able to start another header.
bool is_header_safe(std::string_view s) {
  for (const char c : s) {
    const auto u = static_cast<unsigned char>(c);
    if (u < 0x20 || u == 0x7F) return false;
  }
  return true;
}

constexpr DWORD keepalive_ms = 15000;  // the minimum WinHTTP accepts

}  // namespace

WinHttpTransport::~WinHttpTransport() { close(); }

bool WinHttpTransport::publish(Handle& slot, Handle handle) {
  std::lock_guard lock(mutex_);
  if (stopping_) {
    if (handle != nullptr) WinHttpCloseHandle(as_handle(handle));
    return false;
  }
  slot = handle;
  return handle != nullptr;
}

// Children before parents.
void WinHttpTransport::release_handles() {
  std::lock_guard lock(mutex_);
  for (Handle* slot : {&socket_, &request_, &connection_, &session_}) {
    if (*slot != nullptr) WinHttpCloseHandle(as_handle(*slot));
    *slot = nullptr;
  }
}

void WinHttpTransport::open(const Target& target, std::string_view bearer_token, Sink& sink) {
  close();  // a previous connection, if any, must be gone
  thread_ = std::thread(&WinHttpTransport::run, this, target, std::string(bearer_token), &sink);
}

void WinHttpTransport::close() {
  stopping_ = true;
  {
    // Say goodbye if no send is in flight, then drop the handles; the latter
    // wakes a blocked receive or handshake.
    std::unique_lock send_lock(send_mutex_, std::try_to_lock);
    Handle socket = nullptr;
    {
      std::lock_guard lock(mutex_);
      socket = socket_;
    }
    if (send_lock.owns_lock() && socket != nullptr) {
      WinHttpWebSocketShutdown(as_handle(socket), WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
    }
  }
  release_handles();
  if (thread_.joinable()) thread_.join();
  stopping_ = false;
}

bool WinHttpTransport::send(unsigned type, const void* data, std::size_t size) {
  std::lock_guard send_lock(send_mutex_);
  Handle socket = nullptr;
  {
    std::lock_guard lock(mutex_);
    socket = socket_;
  }
  if (socket == nullptr || stopping_ || size > 0xFFFFFFFFu) return false;
  const DWORD e = WinHttpWebSocketSend(as_handle(socket), static_cast<WINHTTP_WEB_SOCKET_BUFFER_TYPE>(type),
                                       const_cast<void*>(data), static_cast<DWORD>(size));
  return e == NO_ERROR;
}

bool WinHttpTransport::send_text(std::string_view json) {
  return send(WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, json.data(), json.size());
}

bool WinHttpTransport::send_binary(std::span<const std::uint8_t> bytes) {
  return send(WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, bytes.data(), bytes.size());
}

WinHttpTransport::Outcome WinHttpTransport::connect(const Target& target, const std::string& token) {
  if (!is_header_safe(token)) return Outcome::Failed;
  const auto gave_up = [&] { return stopping_ ? Outcome::Stopped : Outcome::Failed; };

  // Each handle is used here by the copy this thread made of it: the members are close()'s to
  // empty from another thread, under the lock.
  const DWORD timeout = static_cast<DWORD>(options_.handshake_timeout.count());
  const HINTERNET session = WinHttpOpen(L"crpp", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!publish(session_, session)) return gave_up();
  WinHttpSetTimeouts(session, static_cast<int>(timeout), static_cast<int>(timeout), static_cast<int>(timeout), static_cast<int>(timeout));
  // The token goes in a header, which a followed redirect would carry to another host.
  DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  if (!WinHttpSetOption(session, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof never)) return gave_up();

  const std::wstring host = widen(target.host);
  const HINTERNET connection = WinHttpConnect(session, host.c_str(), target.port, 0);
  if (!publish(connection_, connection)) return gave_up();
  const std::wstring path = widen(target.path_and_query);
  const HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               target.secure ? WINHTTP_FLAG_SECURE : 0);
  if (!publish(request_, request)) return gave_up();
  if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) return gave_up();

  std::wstring headers = L"Sec-WebSocket-Protocol: " + widen(subprotocol) + L"\r\n";
  if (!token.empty()) headers += L"Authorization: Bearer " + widen(token) + L"\r\n";
  if (!WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(headers.size()), nullptr, 0, 0, 0) ||
      !WinHttpReceiveResponse(request, nullptr)) {
    return gave_up();
  }

  DWORD status = 0;
  DWORD size = sizeof status;
  if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                           WINHTTP_NO_HEADER_INDEX)) {
    return gave_up();
  }
  if (status == 401) return Outcome::Unauthorized;
  if (status != 101) return Outcome::Failed;

  // Exactly the offered subprotocol must have been selected.
  wchar_t selected[64] = {};
  size = sizeof selected;
  if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"Sec-WebSocket-Protocol", selected, &size, WINHTTP_NO_HEADER_INDEX) ||
      widen(subprotocol) != selected) {
    return Outcome::Failed;
  }

  HINTERNET socket = WinHttpWebSocketCompleteUpgrade(request, 0);
  if (socket == nullptr) return gave_up();
  if (!publish(socket_, socket)) return Outcome::Stopped;

  // An idle connection must not time out, whatever the handshake limit was.
  WinHttpSetTimeouts(socket, 0, 0, 0, 0);
  DWORD keepalive = keepalive_ms;
  WinHttpSetOption(socket, WINHTTP_OPTION_WEB_SOCKET_KEEPALIVE_INTERVAL, &keepalive, sizeof keepalive);
  // The request handle is no longer needed.
  {
    std::lock_guard lock(mutex_);
    if (request_ != nullptr) WinHttpCloseHandle(as_handle(request_));
    request_ = nullptr;
  }
  return Outcome::Connected;
}

void WinHttpTransport::receive_loop(Sink& sink) {
  std::size_t used = 0;
  bool discarding = false;
  for (;;) {
    Handle socket = nullptr;
    {
      std::lock_guard lock(mutex_);
      socket = socket_;
    }
    if (socket == nullptr || stopping_) return;

    DWORD n = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
    const DWORD e = WinHttpWebSocketReceive(as_handle(socket), buffer_.data() + used,
                                            static_cast<DWORD>(receive_capacity - used), &n, &type);
    if (e != NO_ERROR || stopping_) return;
    if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) return;

    if (!discarding) used += n;
    const bool fragment = type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE ||
                          type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE;
    if (fragment) {
      if (used >= receive_capacity) {  // too large to keep: drop the whole message
        discarding = true;
        used = 0;
      }
      continue;
    }
    if (!discarding) {
      sink.heard();
      if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
        sink.text(std::string_view(reinterpret_cast<const char*>(buffer_.data()), used));
      } else {
        sink.binary(std::span<const std::uint8_t>(buffer_.data(), used));
      }
    }
    discarding = false;
    used = 0;
  }
}

void WinHttpTransport::run(Target target, std::string token, Sink* sink) {
  const Outcome outcome = connect(target, token);
  if (stopping_ || outcome == Outcome::Stopped) return;
  if (outcome == Outcome::Unauthorized || outcome == Outcome::Failed) {
    release_handles();
    if (!stopping_) sink->connect_failed(outcome == Outcome::Unauthorized);
    return;
  }
  sink->connected();
  receive_loop(*sink);
  if (stopping_) return;
  release_handles();
  sink->closed();
}

}  // namespace crpp::transport
