#include "crpp/assets/windows_platform.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <array>

#include "crpp/update/windows_platform.hpp"

namespace crpp::assets {

namespace {

constexpr int timeout_ms = 15000;

// Small reads, so that progress moves while a large file arrives: the font is 13 MB, and
// read in one piece it showed nothing until the end.
constexpr std::size_t read_size = 64 * 1024;

class Internet {
 public:
  explicit Internet(HINTERNET handle = nullptr) noexcept : handle_(handle) {}
  ~Internet() {
    if (handle_ != nullptr) WinHttpCloseHandle(handle_);
  }
  Internet(const Internet&) = delete;
  Internet& operator=(const Internet&) = delete;
  [[nodiscard]] HINTERNET get() const noexcept { return handle_; }

 private:
  HINTERNET handle_;
};

std::wstring wide(std::string_view s) { return {s.begin(), s.end()}; }  // the URLs here are ASCII

}  // namespace

struct WindowsPlatform::Impl {
  // The session keeps connections alive between files.
  Internet session{WinHttpOpen(L"crpp", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
};

WindowsPlatform::WindowsPlatform() : impl_(std::make_unique<Impl>()) {
  if (impl_->session.get() == nullptr) return;
  WinHttpSetTimeouts(impl_->session.get(), timeout_ms, timeout_ms, timeout_ms, timeout_ms);
  // The files are served directly; a redirect would only hide a change of host.
  DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(impl_->session.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof redirects);
}

WindowsPlatform::~WindowsPlatform() = default;

std::expected<std::vector<std::uint8_t>, Error> WindowsPlatform::fetch(const std::string& url, std::uint64_t limit,
                                                                        const OnBytes& on_bytes) {
  if (impl_->session.get() == nullptr) return std::unexpected(Error::Network);
  const std::wstring address = wide(url);
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof parts;
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts)) return std::unexpected(Error::Network);
  const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  const std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
  const Internet connection(WinHttpConnect(impl_->session.get(), host.c_str(), parts.nPort, 0));
  if (connection.get() == nullptr) return std::unexpected(Error::Network);
  const Internet request(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
  if (request.get() == nullptr ||
      !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.get(), nullptr)) {
    return std::unexpected(Error::Network);
  }
  DWORD status = 0;
  DWORD status_size = sizeof status;
  if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX)) {
    return std::unexpected(Error::Network);
  }
  if (status != 200) return std::unexpected(Error::Unavailable);
  // One byte of room beyond the limit is enough to tell that it is too long.
  const std::size_t room = static_cast<std::size_t>(limit) + 1;
  std::vector<std::uint8_t> bytes;
  std::array<std::uint8_t, read_size> chunk;
  while (bytes.size() < room) {
    DWORD read = 0;
    const DWORD want = static_cast<DWORD>(std::min(chunk.size(), room - bytes.size()));
    if (!WinHttpReadData(request.get(), chunk.data(), want, &read)) return std::unexpected(Error::Network);
    if (read == 0) break;
    if (bytes.empty()) bytes.reserve(std::min<std::size_t>(room, 16 * 1024 * 1024));
    bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + read);
    if (on_bytes) on_bytes(bytes.size());
  }
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

bool WindowsPlatform::verify(const update::PublicKey& key, std::span<const std::uint8_t> message,
                             const update::Signature& signature) {
  return update::verify_signature(key, message, signature);
}

void WindowsPlatform::wait(std::chrono::milliseconds time) { Sleep(static_cast<DWORD>(time.count())); }

}  // namespace crpp::assets
