#include <array>

#include "check.hpp"
#include "crpp/wire/input_frame.hpp"

using namespace crpp::wire;

namespace {

void encode_golden() {
  const std::array events{Kind::KeyNormal, Kind::KeyR1};
  std::array<std::uint8_t, max_frame_events> out{};
  const auto n = encode_client_frame(events, out);
  CHECK(n.has_value() && *n == 2);
  CHECK(out[0] == 16 && out[1] == 40);
}

void encode_accepts_server_only_kinds() {
  const std::array events{static_cast<Kind>(11), static_cast<Kind>(49)};
  std::array<std::uint8_t, max_frame_events> out{};
  CHECK(encode_client_frame(events, out).has_value());
}

void encode_rejects() {
  std::array<std::uint8_t, max_frame_events + 1> out{};
  CHECK(encode_client_frame({}, out).error() == FrameError::Empty);

  const std::array bad{Kind::MouseLeft, static_cast<Kind>(6)};
  CHECK(encode_client_frame(bad, out).error() == FrameError::UnknownKind);

  const std::array<Kind, max_frame_events + 1> many{};
  CHECK(encode_client_frame(many, out).error() == FrameError::TooManyEvents);

  const std::array one{Kind::KeyEnter};
  std::array<std::uint8_t, 0> none{};
  CHECK(encode_client_frame(one, none).error() == FrameError::BufferTooSmall);
}

void encode_exactly_max() {
  std::array<Kind, max_frame_events> events{};
  events.fill(Kind::KeySpace);
  std::array<std::uint8_t, max_frame_events> out{};
  const auto n = encode_client_frame(events, out);
  CHECK(n.has_value() && *n == max_frame_events);
}

void decode_golden() {
  const std::array<std::uint8_t, 4> bytes{3, 16, 0, 44};
  std::array<ServerEvent, max_frame_events> out{};
  const auto n = decode_server_frame(bytes, out);
  CHECK(n.has_value() && *n == 2);
  CHECK((out[0] == ServerEvent{3, Kind::KeyNormal}));
  CHECK((out[1] == ServerEvent{0, Kind::KeySpace}));
}

void decode_rejects() {
  std::array<ServerEvent, max_frame_events> out{};
  CHECK(decode_server_frame({}, out).error() == FrameError::Empty);

  const std::array<std::uint8_t, 3> odd{1, 16, 2};
  CHECK(decode_server_frame(odd, out).error() == FrameError::Misaligned);

  const std::array<std::uint8_t, (max_frame_events + 1) * 2> big{};
  CHECK(decode_server_frame(big, out).error() == FrameError::TooManyEvents);

  const std::array<std::uint8_t, 4> two{1, 16, 2, 17};
  std::array<ServerEvent, 1> small{};
  CHECK(decode_server_frame(two, small).error() == FrameError::BufferTooSmall);
}

void is_known_matches_server_table() {
  // KNOWN_KINDS from upstream server/src/main.rs.
  constexpr std::array<std::uint8_t, 35> table{
      0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 16, 17, 18, 19, 20, 21,
      22, 23, 24, 25, 26, 32, 33, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49};
  std::size_t known = 0;
  for (int v = 0; v < 256; ++v) {
    bool expected = false;
    for (const auto t : table) expected = expected || t == v;
    CHECK(is_known(static_cast<Kind>(v)) == expected);
    known += expected ? 1 : 0;
  }
  CHECK(known == table.size());
}

}  // namespace

int main() {
  encode_golden();
  encode_accepts_server_only_kinds();
  encode_rejects();
  encode_exactly_max();
  decode_golden();
  decode_rejects();
  is_known_matches_server_table();
  return crpp_test_result();
}
