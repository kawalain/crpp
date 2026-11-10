#pragma once
// What every WinHTTP adapter of this program does the same way: owning a handle, opening a
// session with one set of timeouts and one rule for redirects, reading a status and a body up
// to a limit. Each adapter keeps what is its own: the WebSocket upgrade, cancelling, the methods
// of the account API. Windows only; nothing here is reached from a pure module.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace crpp::winhttp {

using Raw = void*;  // HINTERNET

// Closes the handle when it goes.
class Handle {
 public:
  Handle() noexcept = default;
  explicit Handle(Raw handle) noexcept : handle_(handle) {}
  ~Handle() { reset(); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  Handle(Handle&& other) noexcept : handle_(other.release()) {}
  Handle& operator=(Handle&& other) noexcept {
    if (this != &other) reset(other.release());
    return *this;
  }

  [[nodiscard]] Raw get() const noexcept { return handle_; }
  [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
  void reset(Raw handle = nullptr) noexcept;
  [[nodiscard]] Raw release() noexcept {
    const Raw handle = handle_;
    handle_ = nullptr;
    return handle;
  }

 private:
  Raw handle_ = nullptr;
};

// UTF-8 to UTF-16.
[[nodiscard]] std::wstring widen(std::string_view utf8);

// A header's value must not be able to start another header: no control characters.
[[nodiscard]] bool header_safe(std::string_view value) noexcept;

enum class Redirects : std::uint8_t {
  Never,   // a token, a password or a file's source never goes to a host nobody named
  Follow,  // as WinHTTP does by itself, except from HTTPS down to HTTP
};

// A session through the system's proxy settings, with one timeout for resolving, connecting,
// sending and receiving. Empty when it could not be made or the redirect rule could not be set.
[[nodiscard]] Handle open_session(const wchar_t* agent, int timeout_ms, Redirects redirects);

// A host, a port and a path with its query, from an ASCII address.
struct Address {
  std::wstring host, path;
  std::uint16_t port = 0;
  bool secure = false;
};
[[nodiscard]] std::optional<Address> crack(std::string_view url);

// The status of a response that has been received.
[[nodiscard]] std::optional<unsigned long> status_of(Raw request);

// Reads the body to its end, or until it is longer than `limit`: at most limit + 1 bytes, so a
// longer one shows. False when reading failed. on_bytes hears how many have arrived so far.
[[nodiscard]] bool read_body(Raw request, std::size_t limit, std::vector<std::uint8_t>& out,
                             const std::move_only_function<void(std::uint64_t) const>& on_bytes = nullptr);

}  // namespace crpp::winhttp
