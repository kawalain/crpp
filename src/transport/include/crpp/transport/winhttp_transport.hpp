#pragma once
// Windows adapter of Transport, built on the WinHTTP WebSocket API.
//
// One thread per connection blocks in WinHttpWebSocketReceive; it exists only
// while a connection is open. What was verified on Windows 10 19044 against a
// local server (and what the documentation leaves open):
//  - WinHTTP adds the handshake headers itself; Authorization and
//    Sec-WebSocket-Protocol are passed as ordinary request headers, and the
//    selected protocol is readable from the 101 response.
//  - WinHTTP answers server pings itself and never reports them, and it has no
//    way to send a ping. capabilities() therefore reports neither, and the
//    driver must disable Session's ping and liveness timers. The adapter turns
//    on WinHTTP's own 15 s keepalive (an unsolicited pong).
//  - close() unblocks a pending receive by closing the handles from another
//    thread. The documentation forbids that for WinHttpWebSocketClose and is
//    silent for WinHttpCloseHandle; it returned the receive immediately in the
//    tests, which pin this behaviour.
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "crpp/transport/transport.hpp"

namespace crpp::transport {

struct WinHttpOptions {
  // Bound for resolving, connecting and the handshake. Not applied to the
  // established connection, which may idle for any time.
  std::chrono::milliseconds handshake_timeout{15000};
};

class WinHttpTransport final : public Transport {
 public:
  explicit WinHttpTransport(WinHttpOptions options = {}) noexcept : options_(options) {}
  ~WinHttpTransport() override;
  WinHttpTransport(const WinHttpTransport&) = delete;
  WinHttpTransport& operator=(const WinHttpTransport&) = delete;

  [[nodiscard]] Capabilities capabilities() const noexcept override { return {}; }
  void open(const Target& target, std::string_view bearer_token, Sink& sink) override;
  bool send_text(std::string_view json) override;
  bool send_binary(std::span<const std::uint8_t> bytes) override;
  bool ping() override { return true; }  // WinHTTP cannot ping; its keepalive covers it
  // Also stops a connection that is still being established. Blocks until the
  // connection thread has finished; do not call it from a Sink callback.
  void close() override;

 private:
  using Handle = void*;  // HINTERNET

  enum class Outcome : std::uint8_t { Connected, Unauthorized, Failed, Stopped };

  void run(Target target, std::string token, Sink* sink);
  Outcome connect(const Target& target, const std::string& token);
  void receive_loop(Sink& sink);
  bool publish(Handle& slot, Handle handle);
  void release_handles();
  bool send(unsigned type, const void* data, std::size_t size);

  WinHttpOptions options_;
  std::thread thread_;
  std::atomic<bool> stopping_{false};
  std::mutex mutex_;       // guards the four handles
  std::mutex send_mutex_;  // one sender at a time (also vs. shutdown)
  Handle session_ = nullptr;
  Handle connection_ = nullptr;
  Handle request_ = nullptr;
  Handle socket_ = nullptr;
  // Incoming messages are assembled here; larger ones are dropped.
  static constexpr std::size_t receive_capacity = 20 * 1024;
  std::array<std::uint8_t, receive_capacity> buffer_{};
};

}  // namespace crpp::transport
