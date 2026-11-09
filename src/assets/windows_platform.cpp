#include "crpp/assets/windows_platform.hpp"

#include <windows.h>
#include <winhttp.h>

#include "crpp/update/windows_platform.hpp"
#include "crpp/winhttp/winhttp.hpp"

namespace crpp::assets {

namespace {

constexpr int timeout_ms = 15000;

}  // namespace

struct WindowsPlatform::Impl {
  // The session keeps connections alive between files. The files are served directly; a redirect
  // would only hide a change of host.
  winhttp::Handle session = winhttp::open_session(L"crpp", timeout_ms, winhttp::Redirects::Never);
};

WindowsPlatform::WindowsPlatform() : impl_(std::make_unique<Impl>()) {}

WindowsPlatform::~WindowsPlatform() = default;

std::expected<std::vector<std::uint8_t>, Error> WindowsPlatform::fetch(const std::string& url, std::uint64_t limit,
                                                                        const OnBytes& on_bytes) {
  if (!impl_->session) return std::unexpected(Error::Network);
  const auto address = winhttp::crack(url);
  if (!address) return std::unexpected(Error::Network);
  const winhttp::Handle connection(WinHttpConnect(impl_->session.get(), address->host.c_str(), address->port, 0));
  if (!connection) return std::unexpected(Error::Network);
  const winhttp::Handle request(WinHttpOpenRequest(connection.get(), L"GET", address->path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, address->secure ? WINHTTP_FLAG_SECURE : 0));
  if (!request || !WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.get(), nullptr)) {
    return std::unexpected(Error::Network);
  }
  const auto status = winhttp::status_of(request.get());
  if (!status) return std::unexpected(Error::Network);
  if (*status != 200) return std::unexpected(Error::Unavailable);
  std::vector<std::uint8_t> bytes;
  if (!winhttp::read_body(request.get(), static_cast<std::size_t>(limit), bytes, on_bytes)) return std::unexpected(Error::Network);
  return bytes;
}

std::string WindowsPlatform::sha256(std::span<const std::uint8_t> bytes) { return update::sha256_hex(bytes); }

bool WindowsPlatform::verify(const update::PublicKey& key, std::span<const std::uint8_t> message,
                             const update::Signature& signature) {
  return update::verify_signature(key, message, signature);
}

bool WindowsPlatform::write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
  return update::write_flushed(path, bytes);
}

void WindowsPlatform::wait(std::chrono::milliseconds time) { Sleep(static_cast<DWORD>(time.count())); }

}  // namespace crpp::assets
