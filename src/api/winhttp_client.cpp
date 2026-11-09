#include "crpp/api/winhttp_client.hpp"

#include <windows.h>
#include <winhttp.h>

#include <optional>
#include <vector>

#include "crpp/winhttp/winhttp.hpp"

namespace crpp::api {

namespace {

constexpr int timeout_ms = 10000;
// The API's answers are small JSON objects; anything longer is not one of them.
constexpr std::size_t max_body = 64 * 1024;

using winhttp::widen;

}  // namespace

struct WinHttpClient::Impl {
  std::optional<ServerAddress> address;
  winhttp::Handle session, connection;

  bool connect() {
    if (connection) return true;
    if (!address) return false;
    // A redirect is returned, not followed: a password or token goes nowhere but the server chosen.
    session = winhttp::open_session(L"crpp", timeout_ms, winhttp::Redirects::Never);
    if (!session) return false;
    std::wstring host = widen(address->host);
    // WinHTTP takes an IPv6 address without its brackets.
    if (host.size() >= 2 && host.front() == L'[') host = host.substr(1, host.size() - 2);
    const auto port = address->port != 0 ? address->port
                                         : static_cast<std::uint16_t>(address->secure ? INTERNET_DEFAULT_HTTPS_PORT
                                                                                      : INTERNET_DEFAULT_HTTP_PORT);
    connection.reset(WinHttpConnect(session.get(), host.c_str(), port, 0));
    return static_cast<bool>(connection);
  }
};

WinHttpClient::WinHttpClient(std::string_view server_url) : impl_(std::make_unique<Impl>()) {
  impl_->address = parse_server_address(server_url);
}

WinHttpClient::~WinHttpClient() = default;

HttpResult WinHttpClient::send(const Request& request) {
  // The token came from a server's answer: it must not be able to start a header of its own.
  if (!winhttp::header_safe(request.token)) return std::unexpected(HttpError::Network);
  if (!impl_->connect()) return std::unexpected(HttpError::Network);
  const wchar_t* method = request.method == Method::Get ? L"GET" : request.method == Method::Post ? L"POST" : L"PUT";
  const std::wstring path = widen(impl_->address->prefix + request.path);
  const winhttp::Handle handle(WinHttpOpenRequest(impl_->connection.get(), method, path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                  WINHTTP_DEFAULT_ACCEPT_TYPES, impl_->address->secure ? WINHTTP_FLAG_SECURE : 0));
  if (!handle) return std::unexpected(HttpError::Network);
  std::wstring headers;
  if (!request.token.empty()) headers += L"Authorization: Bearer " + widen(request.token) + L"\r\n";
  if (!request.body.empty()) headers += L"Content-Type: application/json\r\n";
  auto* body = request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(request.body.data());
  const auto body_size = static_cast<DWORD>(request.body.size());
  if (!WinHttpSendRequest(handle.get(), headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                          headers.empty() ? 0 : static_cast<DWORD>(-1), body, body_size, body_size, 0) ||
      !WinHttpReceiveResponse(handle.get(), nullptr)) {
    return std::unexpected(HttpError::Network);
  }
  const auto status = winhttp::status_of(handle.get());
  if (!status) return std::unexpected(HttpError::Network);
  std::vector<std::uint8_t> bytes;
  if (!winhttp::read_body(handle.get(), max_body, bytes)) return std::unexpected(HttpError::Network);
  return Response{static_cast<int>(*status), std::string(bytes.begin(), bytes.end())};
}

}  // namespace crpp::api
