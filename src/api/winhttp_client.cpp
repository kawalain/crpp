#include "crpp/api/winhttp_client.hpp"

#include <windows.h>
#include <winhttp.h>

#include <optional>

namespace crpp::api {

namespace {

constexpr int timeout_ms = 10000;
// The API's answers are small JSON objects; anything longer is not one of them.
constexpr std::size_t max_body = 64 * 1024;

class Internet {
 public:
  explicit Internet(HINTERNET handle = nullptr) noexcept : handle_(handle) {}
  ~Internet() {
    if (handle_ != nullptr) WinHttpCloseHandle(handle_);
  }
  Internet(const Internet&) = delete;
  Internet& operator=(const Internet&) = delete;
  Internet& operator=(HINTERNET handle) noexcept {
    if (handle_ != nullptr) WinHttpCloseHandle(handle_);
    handle_ = handle;
    return *this;
  }
  [[nodiscard]] HINTERNET get() const noexcept { return handle_; }

 private:
  HINTERNET handle_;
};

std::wstring wide(std::string_view s) {
  if (s.empty()) return {};
  std::wstring out(static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0)), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), static_cast<int>(out.size()));
  return out;
}

}  // namespace

struct WinHttpClient::Impl {
  std::optional<ServerAddress> address;
  Internet session, connection;

  bool connect() {
    if (connection.get() != nullptr) return true;
    if (!address) return false;
    session = WinHttpOpen(L"crpp", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session.get() == nullptr) return false;
    WinHttpSetTimeouts(session.get(), timeout_ms, timeout_ms, timeout_ms, timeout_ms);
    DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(session.get(), WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof never)) return false;
    std::wstring host = wide(address->host);
    // WinHTTP takes an IPv6 address without its brackets.
    if (host.size() >= 2 && host.front() == L'[') host = host.substr(1, host.size() - 2);
    const auto port = address->port != 0 ? address->port
                                         : static_cast<std::uint16_t>(address->secure ? INTERNET_DEFAULT_HTTPS_PORT
                                                                                      : INTERNET_DEFAULT_HTTP_PORT);
    connection = WinHttpConnect(session.get(), host.c_str(), port, 0);
    return connection.get() != nullptr;
  }
};

WinHttpClient::WinHttpClient(std::string_view server_url) : impl_(std::make_unique<Impl>()) {
  impl_->address = parse_server_address(server_url);
}

WinHttpClient::~WinHttpClient() = default;

HttpResult WinHttpClient::send(const Request& request) {
  if (!impl_->connect()) return std::unexpected(HttpError::Network);
  const wchar_t* method = request.method == Method::Get ? L"GET" : request.method == Method::Post ? L"POST" : L"PUT";
  const std::wstring path = wide(impl_->address->prefix + request.path);
  const Internet handle(WinHttpOpenRequest(impl_->connection.get(), method, path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, impl_->address->secure ? WINHTTP_FLAG_SECURE : 0));
  if (handle.get() == nullptr) return std::unexpected(HttpError::Network);
  std::wstring headers;
  if (!request.token.empty()) headers += L"Authorization: Bearer " + wide(request.token) + L"\r\n";
  if (!request.body.empty()) headers += L"Content-Type: application/json\r\n";
  auto* body = request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(request.body.data());
  const auto body_size = static_cast<DWORD>(request.body.size());
  if (!WinHttpSendRequest(handle.get(), headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                          headers.empty() ? 0 : static_cast<DWORD>(-1), body, body_size, body_size, 0) ||
      !WinHttpReceiveResponse(handle.get(), nullptr)) {
    return std::unexpected(HttpError::Network);
  }
  DWORD status = 0;
  DWORD status_size = sizeof status;
  if (!WinHttpQueryHeaders(handle.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                           &status, &status_size, WINHTTP_NO_HEADER_INDEX)) {
    return std::unexpected(HttpError::Network);
  }
  Response response{static_cast<int>(status), {}};
  char buffer[4096];
  while (response.body.size() < max_body) {
    DWORD read = 0;
    if (!WinHttpReadData(handle.get(), buffer, sizeof buffer, &read)) return std::unexpected(HttpError::Network);
    if (read == 0) break;
    response.body.append(buffer, read);
  }
  return response;
}

}  // namespace crpp::api
