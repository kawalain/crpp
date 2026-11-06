#include <array>
#include <string>
#include <string_view>

#include "check.hpp"
#include "crpp/wire/client_message.hpp"

using namespace crpp::wire;
using namespace std::string_view_literals;

namespace {

std::string encode(const ClientMessage& m) {
  std::array<char, 2048> out{};
  const auto n = encode_client_message(m, out);
  CHECK(n.has_value());
  return n ? std::string(out.data(), *n) : std::string{};
}

void golden() {
  CHECK(encode(cmd::Create{true, false, std::nullopt}) == R"({"t":"create","public":true,"chat":false})");
  CHECK(encode(cmd::Create{false, true, "g"sv}) == R"({"t":"create","public":false,"chat":true,"gid":"g"})");
  CHECK(encode(cmd::Join{"ABC234", std::nullopt}) == R"({"t":"join","code":"ABC234"})");
  CHECK(encode(cmd::Join{"ABC234", "g"sv}) == R"({"t":"join","code":"ABC234","gid":"g"})");
  CHECK(encode(cmd::JoinRandom{std::nullopt}) == R"({"t":"join_random"})");
  CHECK(encode(cmd::Resume{"ABC234", "k", "t"sv, "g"sv}) ==
        R"({"t":"resume","code":"ABC234","key":"k","ticket":"t","gid":"g"})");
  CHECK(encode(cmd::Resume{"ABC234", "k", std::nullopt, std::nullopt}) ==
        R"({"t":"resume","code":"ABC234","key":"k"})");
  CHECK(encode(cmd::Visibility{true}) == R"({"t":"visibility","public":true})");
  CHECK(encode(cmd::ChatEnable{}) == R"({"t":"chat_enable"})");
  CHECK(encode(cmd::ChatDisable{}) == R"({"t":"chat_disable"})");
  CHECK(encode(cmd::Leave{}) == R"({"t":"leave"})");
  CHECK(encode(cmd::Nick{"abc"}) == R"({"t":"nick","nick":"abc"})");
  CHECK(encode(cmd::Sounds{"01_Plastic"sv, std::nullopt, std::nullopt, "1f4a7"sv}) ==
        R"({"t":"sounds","key":"01_Plastic","emoji":"1f4a7"})");
}

void profile_slot_numbers() {
  CHECK(encode(cmd::Profile{0}) == R"({"t":"profile","slot":0})");
  CHECK(encode(cmd::Profile{12}) == R"({"t":"profile","slot":12})");
  CHECK(encode(cmd::Profile{4294967295u}) == R"({"t":"profile","slot":4294967295})");
}

void escaping() {
  CHECK(encode(cmd::Chat{"a\"b\\c\n\r\t\x01\x1f\xC3\xA9"sv}) ==
        "{\"t\":\"chat\",\"text\":\"a\\\"b\\\\c\\n\\r\\t\\u0001\\u001f\xC3\xA9\"}");
}

void buffer_sizes() {
  const ClientMessage m = cmd::Join{"ABC234", "g"sv};
  const std::string expected = encode(m);
  std::array<char, 64> out{};
  for (std::size_t size = 0; size < expected.size(); ++size) {
    const auto r = encode_client_message(m, std::span<char>(out.data(), size));
    CHECK(!r && r.error() == EncodeError::BufferTooSmall);
  }
  const auto exact = encode_client_message(m, std::span<char>(out.data(), expected.size()));
  CHECK(exact && *exact == expected.size());
}

void too_long() {
  std::array<char, 4096> out{};
  const std::string text(max_text_frame_bytes, 'a');  // message is longer with its envelope
  const auto r = encode_client_message(cmd::Chat{text}, out);
  CHECK(!r && r.error() == EncodeError::TooLong);
}

void invalid_utf8() {
  std::array<char, 256> out{};
  CHECK(encode_client_message(cmd::Chat{"\xC3"sv}, out).error() == EncodeError::InvalidUtf8);
  CHECK(encode_client_message(cmd::Chat{"\xC0\x80"sv}, out).error() == EncodeError::InvalidUtf8);
  CHECK(encode_client_message(cmd::Chat{"\xED\xA0\x80"sv}, out).error() == EncodeError::InvalidUtf8);
}

}  // namespace

int main() {
  golden();
  profile_slot_numbers();
  escaping();
  buffer_sizes();
  too_long();
  invalid_utf8();
  return crpp_test_result();
}
