#pragma once

#include <string_view>

namespace hw {

// Returns the hwtool library version, e.g. "0.1.0".
std::string_view version() noexcept;

}  // namespace hw
