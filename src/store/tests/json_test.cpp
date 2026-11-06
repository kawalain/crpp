#include "crpp/store/json.hpp"

#include <string>

#include "check.hpp"

using crpp::store::Json;
using crpp::store::JsonError;

namespace {

JsonError error_of(std::string_view text) {
  const auto parsed = Json::parse(text);
  CHECK(!parsed.has_value());
  return parsed ? JsonError::Syntax : parsed.error();
}

void round_trip_keeps_everything() {
  // Key order, unknown keys and the exact text of numbers all survive.
  const std::string text =
      R"({"z":1,"unknown":{"deep":[1.50,2e3,-0,18446744073709551616]},"a":"한글\n\"q\"","b":true,"c":null,"d":[],"e":{}})";
  const auto parsed = Json::parse(text);
  CHECK(parsed.has_value());
  CHECK(parsed->dump() == text);
}

void pretty_output_parses_back() {
  const auto parsed = Json::parse(R"({"a":[1,{"b":null}],"c":{},"d":[]})");
  CHECK(parsed.has_value());
  const std::string pretty = parsed->dump(true);
  CHECK(pretty == "{\n  \"a\": [\n    1,\n    {\n      \"b\": null\n    }\n  ],\n  \"c\": {},\n  \"d\": []\n}");
  CHECK(Json::parse(pretty) == parsed);
}

void typed_reads() {
  const auto parsed = Json::parse(R"({"n":1700000000000,"f":1.5,"big":18446744073709551616,"s":"x","b":false,"nul":null})");
  CHECK(parsed.has_value());
  CHECK(parsed->find("n")->as_i64() == 1700000000000);
  CHECK(!parsed->find("f")->as_i64());
  CHECK(!parsed->find("big")->as_i64());
  CHECK(parsed->find("f")->number_text() == "1.5");
  CHECK(parsed->find("s")->as_string() == "x");
  CHECK(!parsed->find("s")->as_i64());
  CHECK(parsed->find("b")->as_bool() == false);
  CHECK(parsed->find("nul")->is_null());
  CHECK(parsed->find("missing") == nullptr);
}

void escapes_decode_and_encode() {
  const auto parsed = Json::parse(R"(["\u00e9\ud83d\ude00\/\b\f","\u0001"])");
  CHECK(parsed.has_value());
  CHECK((*parsed->as_array())[0].as_string() == "é😀/\b\f");
  // Control characters come back escaped, so the output is still valid JSON.
  CHECK(Json(std::string("\x01\x1f")).dump() == R"("\u0001\u001f")");
  CHECK(Json::parse(Json(std::string("a\tb\rc")).dump())->as_string() == "a\tb\rc");
}

void object_members_change() {
  Json o = Json::object();
  o.set("a", Json(1));
  o.set("b", Json("x"));
  o.set("a", Json(2));
  CHECK(o.dump() == R"({"a":2,"b":"x"})");
  const auto taken = o.take("a");
  CHECK(taken && taken->as_i64() == 2);
  CHECK(!o.take("a"));
  CHECK(!o.erase("a"));
  CHECK(o.erase("b"));
  CHECK(o.dump() == "{}");
  Json scalar(5);
  CHECK(scalar.find("a") == nullptr);
  scalar.set("a", Json(true));
  CHECK(scalar.dump() == R"({"a":true})");
}

void repeated_key_keeps_the_last() {
  CHECK(Json::parse(R"({"a":1,"b":2,"a":3})")->dump() == R"({"a":3,"b":2})");
}

void malformed_text_is_rejected() {
  for (const std::string_view bad :
       {"", " ", "{", "}", "[", "[1,]", "{\"a\":1,}", "{\"a\"}", "{a:1}", "tru", "nul", "01", "-", "1.", "1e", "+1", "1 2",
        "\"", "\"\\x\"", "\"\\u12\"", "\"a\nb\"", "[1 2]", "{\"a\":1 \"b\":2}"}) {
    CHECK(error_of(bad) == JsonError::Syntax);
  }
}

void bad_unicode_is_rejected() {
  for (const std::string_view bad : {"\"\xff\"", "\"\xc0\x80\"", "\"\xed\xa0\x80\"", "\"\xf4\x90\x80\x80\"", "\"\xe2\x82\"",
                                     R"("\ud83d")", R"("\ude00")", R"("\ud83dx")", R"("\ud83d\u0041")"}) {
    CHECK(error_of(bad) == JsonError::BadUtf8);
  }
}

void nesting_is_bounded() {
  CHECK(Json::parse(std::string(60, '[') + std::string(60, ']')).has_value());
  CHECK(error_of(std::string(100000, '[')) == JsonError::TooDeep);
}

}  // namespace

int main() {
  round_trip_keeps_everything();
  pretty_output_parses_back();
  typed_reads();
  escapes_decode_and_encode();
  object_members_change();
  repeated_key_keeps_the_last();
  malformed_text_is_rejected();
  bad_unicode_is_rejected();
  nesting_is_bounded();
  return crpp_test_result();
}
