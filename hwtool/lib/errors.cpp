#include "hw/errors.hpp"

#include <utility>

namespace hw {

// STUB: placeholder until the real implementation lands.
RouterError::RouterError(std::string code, const std::string& message)
    : std::runtime_error(message), code_(std::move(code)) {}

const std::string& RouterError::code() const noexcept {
    return code_;
}

RouterApiError::RouterApiError(int api_code, const std::string& router_message)
    : RouterError(std::to_string(api_code), router_message), api_code_(api_code) {}

int RouterApiError::api_code() const noexcept {
    return 0;
}

RequestError::RequestError(const std::string& code, const std::string& message)
    : RouterError(code, message) {}

std::optional<std::string_view> api_error_name(int) noexcept {
    return std::nullopt;
}

bool is_auth_error(const RouterError&) noexcept {
    return false;
}

bool is_token_error(const RouterError&) noexcept {
    return false;
}

bool is_connection_error(const RouterError&) noexcept {
    return false;
}

}  // namespace hw
