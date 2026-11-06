#pragma once
// The seam between Session and the network.
//
// A Transport opens one WebSocket at a time and reports what happens to a
// Sink. Callbacks run on a thread owned by the transport; the driver must
// forward them to the thread that owns the Session.
//
// Contract:
//  - open() returns immediately. The outcome is exactly one of connected() or
//    connect_failed(). After connected(), the connection ends with closed()
//    unless close() was called first.
//  - After close() returns, no callback of that connection runs or is running,
//    so a stale event can never reach Session. Consequently close() must not be
//    called from inside a callback.
//  - Views passed to callbacks are valid only during the call.
//  - send_*() and ping() are safe to call from any thread while connected, and
//    return false if the message could not be sent (the connection is then
//    ending; closed() follows).
//  - heard() reports every text/binary frame received, and control frames too
//    if capabilities().reports_control_frames.
//  - Only the cloudroom-v2 subprotocol is offered. If the server picks none,
//    the open fails.
#include <cstdint>
#include <span>
#include <string_view>

#include "crpp/transport/target.hpp"

namespace crpp::transport {

class Sink {
 public:
  virtual void connected() = 0;
  virtual void connect_failed(bool unauthorized) = 0;
  virtual void closed() = 0;
  virtual void text(std::string_view json) = 0;
  virtual void binary(std::span<const std::uint8_t> bytes) = 0;
  virtual void heard() = 0;

 protected:
  ~Sink() = default;
};

// What a transport can do for liveness. A driver that gets false must disable
// the matching Session timers (Timing::ping_interval / liveness_timeout = 0).
struct Capabilities {
  bool can_ping = false;                // ping() really sends a WebSocket ping
  bool reports_control_frames = false;  // heard() also fires for ping/pong frames
};

class Transport {
 public:
  virtual ~Transport() = default;

  [[nodiscard]] virtual Capabilities capabilities() const noexcept = 0;

  // bearer_token may be empty (guest). Opening while a connection exists is a
  // programming error; call close() first.
  virtual void open(const Target& target, std::string_view bearer_token, Sink& sink) = 0;
  virtual bool send_text(std::string_view json) = 0;
  virtual bool send_binary(std::span<const std::uint8_t> bytes) = 0;
  virtual bool ping() = 0;
  virtual void close() = 0;
};

}  // namespace crpp::transport
