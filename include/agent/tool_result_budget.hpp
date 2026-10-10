#pragma once

#include "json.hpp"
#include <cstddef>
#include <string>
#include <string_view>

namespace agent {

// Cut only at a UTF-8 code point boundary.
std::string utf8_prefix(std::string_view text, size_t bytes);

// Bound serialized result content, retaining structured metadata where possible.
JsonValue bound_tool_content(JsonValue content, size_t max_bytes);

} // namespace agent
