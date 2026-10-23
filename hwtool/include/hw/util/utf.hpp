#pragma once

#include <cstddef>
#include <string_view>

namespace hw::util {

// Number of UTF-16 code units in a UTF-8 string (same as JS String.length).
// Throws std::invalid_argument on malformed UTF-8.
std::size_t utf16_length(std::string_view utf8);

}  // namespace hw::util
