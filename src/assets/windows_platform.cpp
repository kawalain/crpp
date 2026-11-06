#include "crpp/assets/windows_platform.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <array>

namespace crpp::assets {

namespace {

constexpr int timeout_ms = 15000;

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

std::wstring wide(std::string_view s) { return {s.begin(), s.end()}; }  // the URLs here are ASCII

}  // namespace

struct WindowsPlatform::Impl {
  // The session keeps the connection alive between files.
  Internet session;
  Internet connection;
  std::wstring prefix;
  bool secure = false;
};

WindowsPlatform::WindowsPlatform(std::string_view base_url) : impl_(std::make_unique<Impl>()) {
  std::wstring url = wide(base_url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof parts;
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return;
  impl_->secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
  impl_->prefix.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
  impl_->session = WinHttpOpen(L"crpp", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0);
  if (impl_->session.get() == nullptr) return;
  WinHttpSetTimeouts(impl_->session.get(), timeout_ms, timeout_ms, timeout_ms, timeout_ms);
  const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  impl_->connection = WinHttpConnect(impl_->session.get(), host.c_str(), parts.nPort, 0);
}

WindowsPlatform::~WindowsPlatform() = default;

std::expected<std::vector<std::uint8_t>, Error> WindowsPlatform::fetch(const Entry& entry, const OnBytes& on_bytes) {
  if (impl_->connection.get() == nullptr) return std::unexpected(Error::Network);
  const std::wstring path = impl_->prefix + wide(entry.path);
  const Internet request(WinHttpOpenRequest(impl_->connection.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, impl_->secure ? WINHTTP_FLAG_SECURE : 0));
  if (request.get() == nullptr ||
      !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.get(), nullptr)) {
    return std::unexpected(Error::Network);
  }
  DWORD status = 0;
  DWORD status_size = sizeof status;
  if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX) ||
      status != 200) {
    return std::unexpected(Error::Network);
  }
  // One byte of room beyond the file is enough to tell that it is too long.
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(entry.size) + 1);
  std::size_t received = 0;
  while (received < bytes.size()) {
    DWORD read = 0;
    if (!WinHttpReadData(request.get(), bytes.data() + received, static_cast<DWORD>(bytes.size() - received), &read)) {
      return std::unexpected(Error::Network);
    }
    if (read == 0) break;
    received += read;
    if (on_bytes) on_bytes(received);
  }
  bytes.resize(received);
  return bytes;
}

std::string WindowsPlatform::sha256(std::span<const std::uint8_t> bytes) {
  std::array<UCHAR, 32> digest{};
  if (!BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<UCHAR*>(bytes.data()),
                                 static_cast<ULONG>(bytes.size()), digest.data(), static_cast<ULONG>(digest.size())))) {
    return {};
  }
  static constexpr char hex[] = "0123456789abcdef";
  std::string text;
  text.reserve(digest.size() * 2);
  for (const UCHAR b : digest) {
    text += hex[b >> 4];
    text += hex[b & 15];
  }
  return text;
}

}  // namespace crpp::assets
