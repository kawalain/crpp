#include "crpp/update/windows_platform.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cstring>

#include "crpp/win/handles.hpp"
#include "crpp/winhttp/winhttp.hpp"

namespace crpp::update {

namespace {

constexpr int timeout_ms = 30000;
constexpr DWORD exit_wait_ms = 30000;

// Owns an ECDSA algorithm provider or key, for the one function that verifies.
void close_algorithm(BCRYPT_ALG_HANDLE algorithm) noexcept { (void)BCryptCloseAlgorithmProvider(algorithm, 0); }
using Algorithm = win::Own<BCRYPT_ALG_HANDLE, close_algorithm>;
void destroy_key(BCRYPT_KEY_HANDLE key) noexcept { (void)BCryptDestroyKey(key); }
using Key = win::Own<BCRYPT_KEY_HANDLE, destroy_key>;

std::wstring ascii_wide(std::string_view s) { return {s.begin(), s.end()}; }

std::wstring ack_event_name(DWORD pid) { return L"Local\\crpp.update." + std::to_wstring(pid); }

}  // namespace

struct WindowsPlatform::Impl {
  std::wstring relaunch_args;
  DWORD launch_wait_ms = 0;
  // GitHub's API refuses requests without a User-Agent; this is it. A release's files are behind a
  // redirect, and what arrives is checked against the signed manifest, so redirects are followed.
  winhttp::Handle session = winhttp::open_session(L"crpp-update", timeout_ms, winhttp::Redirects::Follow);
};

WindowsPlatform::WindowsPlatform(std::wstring relaunch_args, unsigned long launch_wait_ms) : impl_(std::make_unique<Impl>()) {
  impl_->relaunch_args = std::move(relaunch_args);
  impl_->launch_wait_ms = launch_wait_ms;
}

WindowsPlatform::~WindowsPlatform() = default;

std::expected<std::vector<std::uint8_t>, Error> WindowsPlatform::get(const std::string& url, std::size_t limit,
                                                                     std::string_view accept, const OnBytes& on_bytes) {
  if (!impl_->session) return std::unexpected(Error::Network);
  const auto address = winhttp::crack(url);
  if (!address) return std::unexpected(Error::Network);
  const winhttp::Handle connection(WinHttpConnect(impl_->session.get(), address->host.c_str(), address->port, 0));
  if (!connection) return std::unexpected(Error::Network);
  const winhttp::Handle request(WinHttpOpenRequest(connection.get(), L"GET", address->path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                   WINHTTP_DEFAULT_ACCEPT_TYPES, address->secure ? WINHTTP_FLAG_SECURE : 0));
  if (!request) return std::unexpected(Error::Network);
  const std::wstring headers = accept.empty() ? std::wstring{} : L"Accept: " + ascii_wide(accept);
  if (!WinHttpSendRequest(request.get(), headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                          headers.empty() ? 0 : static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.get(), nullptr)) {
    return std::unexpected(Error::Network);
  }
  const auto status = winhttp::status_of(request.get());
  if (!status) return std::unexpected(Error::Network);
  if (*status != 200) return std::unexpected(Error::Unavailable);
  std::vector<std::uint8_t> bytes;
  if (!winhttp::read_body(request.get(), limit, bytes, on_bytes)) return std::unexpected(Error::Network);
  bytes.shrink_to_fit();
  return bytes;
}

std::string WindowsPlatform::sha256(std::span<const std::uint8_t> bytes) { return sha256_hex(bytes); }

std::string sha256_hex(std::span<const std::uint8_t> bytes) {
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

bool WindowsPlatform::verify(const PublicKey& key, std::span<const std::uint8_t> message, const Signature& signature) {
  return verify_signature(key, message, signature);
}

bool verify_signature(const PublicKey& key, std::span<const std::uint8_t> message, const Signature& signature) {
  std::array<UCHAR, 32> digest{};
  if (!BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<UCHAR*>(message.data()),
                                 static_cast<ULONG>(message.size()), digest.data(), static_cast<ULONG>(digest.size())))) {
    return false;
  }
  BCRYPT_ALG_HANDLE raw_algorithm = nullptr;
  if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&raw_algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0))) return false;
  const Algorithm algorithm(raw_algorithm);
  std::array<UCHAR, sizeof(BCRYPT_ECCKEY_BLOB) + std::tuple_size_v<PublicKey>> blob{};
  const BCRYPT_ECCKEY_BLOB header{BCRYPT_ECDSA_PUBLIC_P256_MAGIC, 32};
  std::memcpy(blob.data(), &header, sizeof header);
  std::memcpy(blob.data() + sizeof header, key.data(), key.size());
  BCRYPT_KEY_HANDLE raw_key = nullptr;
  if (!BCRYPT_SUCCESS(BCryptImportKeyPair(algorithm.get(), nullptr, BCRYPT_ECCPUBLIC_BLOB, &raw_key, blob.data(),
                                          static_cast<ULONG>(blob.size()), 0))) {
    return false;
  }
  const Key key_handle(raw_key);
  return BCRYPT_SUCCESS(BCryptVerifySignature(key_handle.get(), nullptr, digest.data(), static_cast<ULONG>(digest.size()),
                                              const_cast<UCHAR*>(signature.data()), static_cast<ULONG>(signature.size()), 0));
}

bool WindowsPlatform::write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) { return write_flushed(path, bytes); }

bool write_flushed(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
  const auto file = win::own_handle(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (file.get() == nullptr) return false;
  std::size_t done = 0;
  while (done < bytes.size()) {
    DWORD written = 0;
    const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - done, 1024 * 1024));
    if (!WriteFile(file.get(), bytes.data() + done, chunk, &written, nullptr) || written != chunk) return false;
    done += written;
  }
  return FlushFileBuffers(file.get()) != 0;
}

std::expected<std::vector<std::uint8_t>, Error> WindowsPlatform::read_file(const std::filesystem::path& path, std::size_t limit) {
  const auto file = win::own_handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr));
  if (file.get() == nullptr) return std::unexpected(Error::Disk);
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file.get(), &size) || static_cast<std::uint64_t>(size.QuadPart) > limit) return std::unexpected(Error::Disk);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));
  std::size_t done = 0;
  while (done < bytes.size()) {
    DWORD read = 0;
    const auto chunk = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - done, 1024 * 1024));
    if (!ReadFile(file.get(), bytes.data() + done, chunk, &read, nullptr) || read == 0) return std::unexpected(Error::Disk);
    done += read;
  }
  return bytes;
}

bool WindowsPlatform::move_file(const std::filesystem::path& from, const std::filesystem::path& to) {
  // A scanner holding a freshly written exe is common and brief.
  for (int attempt = 0; attempt < 10; ++attempt) {
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    const DWORD error = GetLastError();
    if (error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION) return false;
    Sleep(200);
  }
  return false;
}

bool WindowsPlatform::remove_file(const std::filesystem::path& path) {
  if (DeleteFileW(path.c_str())) return true;
  const DWORD error = GetLastError();
  return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

bool WindowsPlatform::launch(const std::filesystem::path& exe) {
  const DWORD me = GetCurrentProcessId();
  const auto started = win::own_handle(CreateEventW(nullptr, TRUE, FALSE, ack_event_name(me).c_str()));
  if (started.get() == nullptr) return false;
  ResetEvent(started.get());  // a failed attempt may have left it set
  std::wstring command = L"\"" + exe.wstring() + L"\" --updated-from " + std::to_wstring(me);
  if (!impl_->relaunch_args.empty()) command += L" " + impl_->relaunch_args;
  STARTUPINFOW startup{};
  startup.cb = sizeof startup;
  PROCESS_INFORMATION process{};
  // A file that is not a program makes CreateProcessW show a modal "Unsupported 16-Bit
  // Application" box and wait for it, unless this thread fails critical errors itself
  // (seen on Windows Server 2025 with error mode 0). The failure is then just a return value.
  DWORD error_mode = 0;
  const bool quiet = SetThreadErrorMode(SEM_FAILCRITICALERRORS, &error_mode) != FALSE;
  const BOOL created = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                      exe.parent_path().c_str(), &startup, &process);
  if (quiet) SetThreadErrorMode(error_mode, nullptr);
  if (!created) return false;
  const win::Handle child(process.hProcess), thread(process.hThread);
  const std::array<HANDLE, 2> waits{started.get(), child.get()};
  if (WaitForMultipleObjects(static_cast<DWORD>(waits.size()), waits.data(), FALSE, impl_->launch_wait_ms) == WAIT_OBJECT_0) {
    return true;
  }
  TerminateProcess(child.get(), 1);
  WaitForSingleObject(child.get(), 5000);  // until the file it ran from can be replaced
  return false;
}

std::filesystem::path running_exe() {
  std::wstring buffer(32768, L'\0');
  const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  buffer.resize(n);
  return buffer;
}

void report_started(unsigned long parent_pid) {
  const auto started = win::own_handle(OpenEventW(EVENT_MODIFY_STATE, FALSE, ack_event_name(parent_pid).c_str()));
  if (started.get() != nullptr) SetEvent(started.get());
}

void wait_for_exit(unsigned long parent_pid) {
  const auto parent = win::own_handle(OpenProcess(SYNCHRONIZE, FALSE, parent_pid));
  if (parent.get() != nullptr) WaitForSingleObject(parent.get(), exit_wait_ms);
}

}  // namespace crpp::update
