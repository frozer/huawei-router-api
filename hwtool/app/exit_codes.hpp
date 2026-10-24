#pragma once

#include <exception>
#include <string>

namespace hwtool {

enum class ExitCode : int {
    ok = 0,
    failure = 1,
    usage = 2,
    network = 3,
    auth = 4,
    router = 5,
    partial = 6
};

// std::invalid_argument -> usage; RouterError with code "invalid_router_url" -> usage;
// is_connection_error -> network; is_auth_error -> auth; any other hw::RouterError -> router;
// anything else -> failure.
ExitCode exit_code_for(const std::exception& e) noexcept;

// RouterError -> code(); std::invalid_argument -> "invalid_argument"; else "internal".
std::string error_code_for(const std::exception& e);

}  // namespace hwtool
