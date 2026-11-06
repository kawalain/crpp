#include "crpp/wire/input_frame.hpp"

namespace crpp::wire {

std::expected<std::size_t, FrameError> encode_client_frame(
    std::span<const Kind> events, std::span<std::uint8_t> out) noexcept {
  if (events.empty()) return std::unexpected(FrameError::Empty);
  if (events.size() > max_frame_events) return std::unexpected(FrameError::TooManyEvents);
  if (out.size() < events.size()) return std::unexpected(FrameError::BufferTooSmall);
  for (const Kind kind : events) {
    if (!is_known(kind)) return std::unexpected(FrameError::UnknownKind);
  }
  for (std::size_t i = 0; i < events.size(); ++i) {
    out[i] = static_cast<std::uint8_t>(events[i]);
  }
  return events.size();
}

std::expected<std::size_t, FrameError> decode_server_frame(
    std::span<const std::uint8_t> bytes, std::span<ServerEvent> out) noexcept {
  constexpr std::size_t event_size = 2;
  if (bytes.empty()) return std::unexpected(FrameError::Empty);
  if (bytes.size() % event_size != 0) return std::unexpected(FrameError::Misaligned);
  const std::size_t count = bytes.size() / event_size;
  if (count > max_frame_events) return std::unexpected(FrameError::TooManyEvents);
  if (out.size() < count) return std::unexpected(FrameError::BufferTooSmall);
  for (std::size_t i = 0; i < count; ++i) {
    out[i] = ServerEvent{bytes[i * event_size], static_cast<Kind>(bytes[i * event_size + 1])};
  }
  return count;
}

}  // namespace crpp::wire
