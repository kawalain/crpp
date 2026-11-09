#pragma once
// One question to the server: does GET /health answer with success, and with what? Blocking,
// so a caller runs it off the UI thread. Upstream gives the answer 5 s.
#include <chrono>
#include <mutex>
#include <optional>
#include <string>

#include "crpp/transport/target.hpp"

namespace crpp::transport {

class WinHttpHealth {
 public:
  WinHttpHealth() = default;
  explicit WinHttpHealth(std::chrono::milliseconds timeout) : timeout_(timeout) {}
  ~WinHttpHealth() { cancel(); }
  WinHttpHealth(const WinHttpHealth&) = delete;
  WinHttpHealth& operator=(const WinHttpHealth&) = delete;

  // False for anything but a 2xx answer, and after cancel().
  [[nodiscard]] bool reachable(const Target& target) { return ask(target).has_value(); }
  // The body of a 2xx answer, its first KB, or nothing for anything else and after cancel().
  [[nodiscard]] std::optional<std::string> ask(const Target& target);
  // Any thread: a running reachable() returns soon, and later ones at once.
  void cancel();

 private:
  using Handle = void*;  // HINTERNET

  bool publish(Handle& slot, Handle handle);
  void release();

  std::chrono::milliseconds timeout_{5000};
  std::mutex mutex_;
  bool cancelled_ = false;
  Handle session_ = nullptr, connection_ = nullptr, request_ = nullptr;
};

}  // namespace crpp::transport
