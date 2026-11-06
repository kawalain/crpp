#include <algorithm>
#include <array>
#include <cstdio>
#include <source_location>
#include <string>
#include <string_view>

#include "check.hpp"
#include "crpp/wire/server_message.hpp"

using namespace crpp::wire;
using namespace std::string_view_literals;

namespace {

struct Bufs {
  std::array<char, 4096> text{};
  std::array<MemberInfo, 4> members{};
};

// The scratch is exactly as large as the input: this is the documented minimum.
std::expected<ServerMessage, ParseError> parse(std::string_view json, Bufs& b) {
  return parse_server_message(json, ParseBuffers{std::span<char>(b.text.data(), json.size()), b.members});
}

// Takes the input as an argument because CHECK stringizes its expression and
// MSVC cannot stringize raw string literals that contain backslashes.
void expect_error(std::string_view json, ParseError expected,
                  std::source_location loc = std::source_location::current()) {
  Bufs b;
  const auto r = parse(json, b);
  if (r || r.error() != expected) {
    ++crpp::test::failures;
    std::fprintf(stderr, "%s:%u: expected error %d for: %.*s\n", loc.file_name(), loc.line(),
                 static_cast<int>(expected), static_cast<int>(json.size()), json.data());
  }
}

template <class T>
const T* as(const std::expected<ServerMessage, ParseError>& r) {
  return r ? std::get_if<T>(&*r) : nullptr;
}

constexpr auto joined_json =
    R"({"t":"joined","code":"ABC234","me":1,"members":[)"
    R"({"slot":0,"order":1,"offline":false,"nick":"a","cid":"c0","emoji":"1f4a7","key":"01_Plastic"},)"
    R"({"slot":1,"order":2,"offline":true,"reconnected":true,"nick":"b","cid":"c1","uid":"u","mouse":"m","pen":"p"})"
    R"(],"host":0,"public":true,"chat":false,"chatLock":300})"sv;

void joined_golden() {
  Bufs b;
  const auto r = parse(joined_json, b);
  const auto* j = as<msg::Joined>(r);
  CHECK(j != nullptr);
  if (!j) return;
  CHECK(j->code == "ABC234");
  CHECK(j->me == 1);
  CHECK(j->room.host == 0u);
  CHECK(j->room.is_public && !j->room.chat);
  CHECK(j->room.chat_lock == 300u);
  CHECK(j->room.members.size() == 2);
  const MemberInfo& a = j->room.members[0];
  CHECK(a.slot == 0 && a.order == 1 && !a.offline && !a.reconnected);
  CHECK(a.nick == "a" && a.cid == "c0");
  CHECK(a.emoji == "1f4a7" && a.key == "01_Plastic");
  CHECK(!a.uid && !a.mouse && !a.pen);
  const MemberInfo& c = j->room.members[1];
  CHECK(c.slot == 1 && c.offline && c.reconnected);
  CHECK(c.uid == "u" && c.mouse == "m" && c.pen == "p" && !c.emoji);
}

void members_update() {
  Bufs b;
  const auto r = parse(R"({"t":"members","members":[],"host":3,"public":false,"chat":true})"sv, b);
  const auto* m = as<msg::Members>(r);
  CHECK(m != nullptr);
  if (!m) return;
  CHECK(m->room.members.empty() && m->room.host == 3u && m->room.chat && !m->room.is_public);
  CHECK(!m->room.chat_lock);
}

void simple_messages() {
  Bufs b;
  const auto gid = parse(R"({"t":"gid","gid":"tok"})"sv, b);
  CHECK(as<msg::Gid>(gid) && as<msg::Gid>(gid)->gid == "tok");

  const auto resume = parse(R"({"t":"resume","code":"ABC234","key":"K"})"sv, b);
  CHECK(as<msg::Resume>(resume) && as<msg::Resume>(resume)->key == "K");

  const auto ticket = parse(R"({"t":"ticket","code":"ABC234","ticket":"a.b.c"})"sv, b);
  CHECK(as<msg::Ticket>(ticket) && as<msg::Ticket>(ticket)->ticket == "a.b.c");

  const auto blocked = parse(R"({"t":"chat_blocked","ms":5000})"sv, b);
  CHECK(as<msg::ChatBlocked>(blocked) && as<msg::ChatBlocked>(blocked)->ms == 5000);

  const auto left = parse(R"({"t":"left"})"sv, b);
  CHECK(as<msg::Left>(left) != nullptr);

  const auto error = parse(R"({"t":"error","error":"room_full"})"sv, b);
  CHECK(as<msg::Error>(error) && as<msg::Error>(error)->error == "room_full");
}

void chat_and_profile() {
  Bufs b;
  const auto chat = parse(
      R"({"t":"chat","slot":2,"cid":"c","nick":"n","emoji":null,"text":"hi","time":1700000000000})"sv, b);
  const auto* c = as<msg::Chat>(chat);
  CHECK(c != nullptr);
  if (c) {
    CHECK(c->slot == 2 && c->text == "hi" && !c->emoji && c->time == 1700000000000);
  }

  const auto profile = parse(
      R"({"t":"profile","slot":1,"nick":"n","emoji":"1f600","handle":null,"created":null,"stayed":65,"events":9})"sv, b);
  const auto* p = as<msg::Profile>(profile);
  CHECK(p != nullptr);
  if (p) {
    CHECK(p->emoji == "1f600" && !p->handle && !p->created && p->stayed == 65 && p->events == 9);
  }
}

void string_escapes_and_utf8() {
  Bufs b;
  // "b\"q" + e-acute + U+1F600 (surrogate pair) + newline, then raw UTF-8.
  const auto r = parse(
      "{\"t\":\"error\",\"error\":\"b\\\"q\\u00e9\\ud83d\\ude00\\n\xE2\x82\xAC\"}"sv, b);
  const auto* e = as<msg::Error>(r);
  CHECK(e != nullptr);
  if (e) CHECK(e->error == "b\"q\xC3\xA9\xF0\x9F\x98\x80\n\xE2\x82\xAC"sv);
}

void unknown_type_and_fields_are_ignored() {
  Bufs b;
  const auto unknown = parse(R"({"t":"future_thing","x":[1,{"y":[true,null]}]})"sv, b);
  CHECK(as<msg::Unknown>(unknown) != nullptr);

  const auto extra = parse(
      R"({"extra":{"a":[1,2.5e3,-0,"s\n"]},"t":"left","more":false})"sv, b);
  CHECK(as<msg::Left>(extra) != nullptr);

  const auto t_last = parse(R"({"gid":"g","t":"gid"})"sv, b);
  CHECK(as<msg::Gid>(t_last) != nullptr);

  const auto ws = parse(" \n{ \"t\" : \"left\" ,\t\"x\" : 1 } \r\n"sv, b);
  CHECK(as<msg::Left>(ws) != nullptr);
}

void semantic_errors() {
  Bufs b;
  expect_error(R"({})"sv, ParseError::MissingField);
  expect_error(R"({"t":"gid"})"sv, ParseError::MissingField);
  expect_error(R"({"t":"gid","gid":null})"sv, ParseError::MissingField);
  expect_error(R"({"t":"joined","code":"A","members":[]})"sv, ParseError::MissingField);
  CHECK(parse(R"({"t":"members","members":[{"slot":0,"nick":"a"}]})"sv, b).error() ==
        ParseError::MissingField);
  expect_error(R"({"t":1})"sv, ParseError::Type);
  expect_error(R"([])"sv, ParseError::Type);
  expect_error(R"("x")"sv, ParseError::Type);
  expect_error(R"({"t":"gid","gid":5})"sv, ParseError::Type);

  std::string many = R"({"t":"members","members":[)";
  for (int i = 0; i < 5; ++i) {
    many += R"({"slot":0,"nick":"a","cid":"c"})";
    if (i < 4) many += ',';
  }
  many += "]}";
  CHECK(parse(many, b).error() == ParseError::TooManyMembers);
}

void syntax_errors() {
  Bufs b;
  expect_error(""sv, ParseError::Syntax);
  expect_error(R"({"t":"left"} x)"sv, ParseError::TrailingData);
  expect_error(R"({"t":"left",})"sv, ParseError::Syntax);
  expect_error(R"({"t" "left"})"sv, ParseError::Syntax);
  expect_error(R"({"t":"le\x"})"sv, ParseError::Escape);
  expect_error("{\"t\":\"a\nb\"}"sv, ParseError::Syntax);
  expect_error(R"({"t":"\ud83d"})"sv, ParseError::Escape);
  expect_error(R"({"t":"\ude00"})"sv, ParseError::Escape);
  expect_error("{\"t\":\"\xC3\"}"sv, ParseError::Utf8);
  expect_error("{\"t\":\"\xC0\x80\"}"sv, ParseError::Utf8);
  expect_error("{\"t\":\"\xED\xA0\x80\"}"sv, ParseError::Utf8);
  expect_error(R"({"t":"left","x":tru})"sv, ParseError::Syntax);
  expect_error(R"({"t":"left","x":01})"sv, ParseError::Number);
  expect_error(R"({"t":"left","x":1.})"sv, ParseError::Number);
}

void number_errors() {
  Bufs b;
  expect_error(R"({"t":"profile","slot":1.5,"nick":"n"})"sv, ParseError::Number);
  expect_error(R"({"t":"profile","slot":-1,"nick":"n"})"sv, ParseError::Number);
  expect_error(R"({"t":"profile","slot":4294967296,"nick":"n"})"sv, ParseError::Number);
  expect_error(R"({"t":"profile","slot":01,"nick":"n"})"sv, ParseError::Number);
  expect_error(R"({"t":"chat_blocked","ms":18446744073709551616})"sv, ParseError::Number);
  CHECK(as<msg::ChatBlocked>(parse(R"({"t":"chat_blocked","ms":18446744073709551615})"sv, b)));
  CHECK(as<msg::Chat>(parse(R"({"t":"chat","slot":0,"cid":"c","nick":"n","text":"t","time":-9223372036854775808})"sv, b)));
  CHECK(parse(R"({"t":"chat","slot":0,"cid":"c","nick":"n","text":"t","time":9223372036854775808})"sv, b).error() ==
        ParseError::Number);
}

void depth_limit() {
  Bufs b;
  std::string ok = R"({"t":"left","x":)" + std::string(10, '[') + std::string(10, ']') + "}";
  CHECK(as<msg::Left>(parse(ok, b)) != nullptr);
  std::string deep = R"({"t":"left","x":)" + std::string(40, '[') + std::string(40, ']') + "}";
  CHECK(parse(deep, b).error() == ParseError::Depth);
}

void scratch_too_small() {
  std::array<char, 3> tiny{};
  std::array<MemberInfo, 1> members{};
  const auto r = parse_server_message(R"({"t":"left"})"sv, ParseBuffers{tiny, members});
  CHECK(!r && r.error() == ParseError::ScratchTooSmall);
}

// Every strict prefix of a complete message is invalid; no input may crash.
void truncation_and_mutation() {
  Bufs b;
  const std::array<std::string_view, 3> goldens{
      joined_json,
      R"({"t":"chat","slot":2,"cid":"c","nick":"n","text":"hé","time":1})"sv,
      R"({"t":"profile","slot":1,"nick":"n","stayed":65,"events":9,"x":[1,{"y":null}]})"sv};
  for (const auto json : goldens) {
    CHECK(parse(json, b).has_value());
    for (std::size_t len = 0; len < json.size(); ++len) {
      CHECK(!parse(json.substr(0, len), b).has_value());
    }
    std::string mutated(json);
    for (std::size_t i = 0; i < mutated.size(); ++i) {
      const char saved = mutated[i];
      for (const char c : {'"', '\\', '\xFF', '{', '}', '[', ']', ',', ':', '\0', '9'}) {
        mutated[i] = c;
        (void)parse(mutated, b);
      }
      mutated[i] = saved;
    }
  }
}

}  // namespace

int main() {
  joined_golden();
  members_update();
  simple_messages();
  chat_and_profile();
  string_escapes_and_utf8();
  unknown_type_and_fields_are_ignored();
  semantic_errors();
  syntax_errors();
  number_errors();
  depth_limit();
  scratch_too_small();
  truncation_and_mutation();
  return crpp_test_result();
}
