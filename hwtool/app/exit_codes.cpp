#include "exit_codes.hpp"

// Step 6a stub: inert until the real implementation lands.

namespace hwtool {

ExitCode exit_code_for(const std::exception&) noexcept {
    return ExitCode::ok;
}

std::string error_code_for(const std::exception&) {
    return "";
}

}  // namespace hwtool
