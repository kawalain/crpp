#pragma once
// Input frames of the cloudroom-v2 subprotocol (binary WebSocket frames).
//   client -> server: [kind]*          (1 byte per event)
//   server -> client: [slot, kind]*    (2 bytes per event)
// Pure functions over caller-provided buffers; nothing allocates.
// Spec: upstream server/src/main.rs (KNOWN_KINDS, MAX_FRAME_EVENTS).
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace crpp::wire {

enum class Kind : std::uint8_t {
  MouseLeft = 0,
  MouseMiddle = 1,
  MouseWheel = 2,
  MouseRight = 3,
  MouseSideBack = 4,
  MouseSideForward = 5,
  MouseLeftUp = 8,
  MouseMiddleUp = 9,
  MouseRightUp = 10,
  MouseLeftHold = 11,
  MouseMiddleHold = 12,
  MouseRightHold = 13,
  KeyNormal = 16,
  KeyTab = 17,
  KeyCaps = 18,
  KeyShiftHold = 19,
  KeyCtrlHold = 20,
  KeyAltHold = 21,
  KeyEnter = 22,
  KeyEscape = 23,
  KeyShift = 24,
  KeyCtrl = 25,
  KeyAlt = 26,
  PenTap = 32,
  PenStroke = 33,
  KeyR1 = 40,
  KeyR2 = 41,
  KeyR3 = 42,
  KeyR4 = 43,
  KeySpace = 44,
  KeyBackspace = 45,
  KeyShiftUp = 46,
  KeyCtrlUp = 47,
  KeyAltUp = 48,
  KeyAny = 49,  // a key press that says nothing about the key
};

inline constexpr std::size_t max_frame_events = 32;

// A key going down, counted once: not a release and not a modifier still held.
[[nodiscard]] constexpr bool is_key_press(Kind kind) noexcept {
  switch (kind) {
    case Kind::KeyNormal: case Kind::KeyTab: case Kind::KeyCaps: case Kind::KeyEnter: case Kind::KeyEscape:
    case Kind::KeyShift: case Kind::KeyCtrl: case Kind::KeyAlt: case Kind::KeyR1: case Kind::KeyR2: case Kind::KeyR3:
    case Kind::KeyR4: case Kind::KeySpace: case Kind::KeyBackspace: case Kind::KeyAny:
      return true;
    default:
      return false;
  }
}

// A mouse button or the Pen's tip going down; not the wheel, a release or a hold.
[[nodiscard]] constexpr bool is_click(Kind kind) noexcept {
  switch (kind) {
    case Kind::MouseLeft: case Kind::MouseMiddle: case Kind::MouseRight: case Kind::MouseSideBack:
    case Kind::MouseSideForward: case Kind::PenTap:
      return true;
    default:
      return false;
  }
}

// True for every kind the server accepts.
[[nodiscard]] constexpr bool is_known(Kind kind) noexcept {
  switch (static_cast<std::uint8_t>(kind)) {
    case 0: case 1: case 2: case 3: case 4: case 5:
    case 8: case 9: case 10: case 11: case 12: case 13:
    case 16: case 17: case 18: case 19: case 20: case 21:
    case 22: case 23: case 24: case 25: case 26:
    case 32: case 33:
    case 40: case 41: case 42: case 43: case 44: case 45:
    case 46: case 47: case 48: case 49:
      return true;
    default:
      return false;
  }
}

enum class FrameError : std::uint8_t {
  Empty,           // no events
  TooManyEvents,   // more than max_frame_events
  Misaligned,      // byte length is not a multiple of the event size
  UnknownKind,     // encode only: the server would drop this event
  BufferTooSmall,  // caller-provided output is too small
};

struct ServerEvent {
  std::uint8_t slot;
  Kind kind;
  friend constexpr bool operator==(const ServerEvent&, const ServerEvent&) = default;
};

// Writes one byte per event into out; returns the number of bytes written.
[[nodiscard]] std::expected<std::size_t, FrameError> encode_client_frame(
    std::span<const Kind> events, std::span<std::uint8_t> out) noexcept;

// Reads [slot, kind]* events into out; returns the number of events decoded.
// Unknown kinds are kept as received; the consumer decides what to do.
[[nodiscard]] std::expected<std::size_t, FrameError> decode_server_frame(
    std::span<const std::uint8_t> bytes, std::span<ServerEvent> out) noexcept;

}  // namespace crpp::wire
