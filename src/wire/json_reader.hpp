#pragma once
// Private to the wire module: a pull parser over a JSON text.
//
// Errors are latched: after the first failure every call is a no-op that
// returns a neutral value, so callers read a whole structure and check ok()
// once. Callers must not use values read after ok() turned false.
//
// Strings are decoded into the caller's scratch buffer. Decoding never grows
// a string, so a scratch of text.size() bytes always suffices. Returned views
// point into scratch and stay valid for the Reader's life, except a key from
// next_key(), which is valid only until the next read.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "crpp/wire/parse_error.hpp"

namespace crpp::wire::detail {

class Reader {
 public:
  static constexpr std::size_t max_depth = 16;

  Reader(std::string_view text, std::span<char> scratch) noexcept
      : text_(text), scratch_(scratch) {}

  [[nodiscard]] bool ok() const noexcept { return !failed_; }
  [[nodiscard]] ParseError error() const noexcept { return error_; }
  void fail(ParseError e) noexcept;

  void begin_object() noexcept;
  // Next key of the current object, or nullopt at its end (or on error).
  std::optional<std::string_view> next_key() noexcept;
  void begin_array() noexcept;
  // True if another element follows (the caller then reads it).
  bool next_element() noexcept;

  std::string_view string() noexcept;
  // Consumes a `null` value if one is next; returns whether it did.
  bool null() noexcept;
  std::uint64_t u64() noexcept;
  std::int64_t i64() noexcept;
  bool boolean() noexcept;
  void skip() noexcept;
  // Succeeds only if nothing but whitespace remains.
  void finish() noexcept;

 private:
  void skip_ws() noexcept;
  bool expect(char c, ParseError otherwise) noexcept;
  bool put(char c) noexcept;
  bool read_escape() noexcept;
  bool hex4(char32_t& out) noexcept;
  bool put_utf8(char32_t cp) noexcept;
  bool match_literal(std::string_view literal) noexcept;
  bool parse_integer(bool& negative, std::uint64_t& magnitude) noexcept;
  void skip_number() noexcept;

  std::string_view text_;
  std::span<char> scratch_;
  std::size_t pos_ = 0;
  std::size_t used_ = 0;
  std::array<bool, max_depth> first_{};  // per open container: nothing read yet
  std::size_t depth_ = 0;
  bool failed_ = false;
  ParseError error_ = ParseError::Syntax;
};

}  // namespace crpp::wire::detail
