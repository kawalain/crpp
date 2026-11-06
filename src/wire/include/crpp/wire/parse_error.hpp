#pragma once
#include <cstdint>

namespace crpp::wire {

enum class ParseError : std::uint8_t {
  Syntax,           // not well-formed JSON
  Utf8,             // invalid UTF-8 inside a string
  Escape,           // bad \ escape or unpaired surrogate
  Number,           // malformed, fractional or out-of-range number
  Depth,            // nesting deeper than the parser allows
  ScratchTooSmall,  // ParseBuffers::text smaller than the input
  Type,             // a value has the wrong JSON type
  MissingField,     // a required field is absent (or null)
  TooManyMembers,   // more members than ParseBuffers::members can hold
  TrailingData,     // data after the top-level value
};

}  // namespace crpp::wire
