#pragma once

#include <charconv>
#include <string>
#include <string_view>

#include "hw/errors.hpp"

// Private helpers for reading router response fields. Not part of the public API.
namespace hw::util {

// Strips leading and trailing ASCII whitespace.
inline std::string_view trim(std::string_view text) {
    constexpr std::string_view kSpace = " \t\r\n\f\v";
    const auto first = text.find_first_not_of(kSpace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(kSpace);
    return text.substr(first, last - first + 1);
}

// Strict base-10 integer: the whole trimmed text must be consumed. Throws
// RouterError("invalid_response") naming field for empty, partial or out-of-range input.
inline int parse_int(std::string_view field, std::string_view text) {
    const std::string_view value = trim(text);
    int result = 0;
    const char* const last = value.data() + value.size();
    const auto [end, ec] = std::from_chars(value.data(), last, result);
    if (value.empty() || ec != std::errc() || end != last) {
        throw RouterError("invalid_response", std::string(field) + ": " + std::string(text));
    }
    return result;
}

}  // namespace hw::util
