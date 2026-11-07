#pragma once
// JSON Schemas of a pack's files, for editors and for whoever writes one. They are written from what
// the loader takes, so they cannot say otherwise; skins/schema holds them, and a test compares.
// What they cannot check, such as the names inside an expression, the loader still refuses.
#include <string>

#include "crpp/skin/pack.hpp"

namespace crpp::skin {

// common.schema.json: what the others refer to.
[[nodiscard]] std::string common_json_schema();
// pack.schema.json.
[[nodiscard]] std::string pack_json_schema();
// screen.<name>.schema.json.
[[nodiscard]] std::string screen_json_schema(const ScreenSchema& schema);

}  // namespace crpp::skin
