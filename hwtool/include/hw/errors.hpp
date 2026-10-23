#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace hw {

// Base error type. code() is a stable machine-readable identifier.
class RouterError : public std::runtime_error {
public:
    RouterError(std::string code, const std::string& message);

    const std::string& code() const noexcept;

private:
    std::string code_;
};

// Error reported by the router in an API response (mirrors throwApiError in src/error.js).
class RouterApiError : public RouterError {
public:
    explicit RouterApiError(int api_code, const std::string& router_message = "");

    int api_code() const noexcept;

private:
    int api_code_;
};

// Transport-level failure; code() is "http_request_" + code.
class RequestError : public RouterError {
public:
    RequestError(const std::string& code, const std::string& message);
};

// Name of a router API error code, e.g. 108002 -> "login_password_wrong".
std::optional<std::string_view> api_error_name(int api_code) noexcept;

// True for router API errors 108001..108007.
bool is_auth_error(const RouterError& e) noexcept;

// True for router API errors 125001..125003.
bool is_token_error(const RouterError& e) noexcept;

// True for transport errors (code starts with "http_request_").
bool is_connection_error(const RouterError& e) noexcept;

}  // namespace hw
