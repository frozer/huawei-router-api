#include "exit_codes.hpp"

#include <stdexcept>
#include <string>

#include "hw/errors.hpp"

namespace hwtool {

ExitCode exit_code_for(const std::exception& e) noexcept {
    if (dynamic_cast<const std::invalid_argument*>(&e) != nullptr) {
        return ExitCode::usage;
    }
    if (const auto* router = dynamic_cast<const hw::RouterError*>(&e)) {
        if (router->code() == "invalid_router_url") {
            return ExitCode::usage;
        }
        if (hw::is_connection_error(*router)) {
            return ExitCode::network;
        }
        if (hw::is_auth_error(*router)) {
            return ExitCode::auth;
        }
        return ExitCode::router;
    }
    return ExitCode::failure;
}

std::string error_code_for(const std::exception& e) {
    if (const auto* router = dynamic_cast<const hw::RouterError*>(&e)) {
        return router->code();
    }
    if (dynamic_cast<const std::invalid_argument*>(&e) != nullptr) {
        return "invalid_argument";
    }
    return "internal";
}

}  // namespace hwtool
