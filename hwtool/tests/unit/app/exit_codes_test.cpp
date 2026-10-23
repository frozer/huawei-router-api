#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

#include "exit_codes.hpp"
#include "hw/errors.hpp"

using hwtool::error_code_for;
using hwtool::ExitCode;
using hwtool::exit_code_for;

TEST_CASE("ExitCode values match the documented contract", "[exit_codes]") {
    CHECK(static_cast<int>(ExitCode::ok) == 0);
    CHECK(static_cast<int>(ExitCode::failure) == 1);
    CHECK(static_cast<int>(ExitCode::usage) == 2);
    CHECK(static_cast<int>(ExitCode::network) == 3);
    CHECK(static_cast<int>(ExitCode::auth) == 4);
    CHECK(static_cast<int>(ExitCode::router) == 5);
    CHECK(static_cast<int>(ExitCode::partial) == 6);
}

TEST_CASE("Usage errors map to usage", "[exit_codes]") {
    CHECK(exit_code_for(std::invalid_argument("bad")) == ExitCode::usage);
    CHECK(exit_code_for(hw::RouterError("invalid_router_url", "x")) == ExitCode::usage);
}

TEST_CASE("Transport errors map to network", "[exit_codes]") {
    CHECK(exit_code_for(hw::RequestError("timeout", "")) == ExitCode::network);
}

TEST_CASE("Router API auth errors map to auth", "[exit_codes]") {
    CHECK(exit_code_for(hw::RouterApiError(108002)) == ExitCode::auth);
}

TEST_CASE("Other router errors map to router", "[exit_codes]") {
    CHECK(exit_code_for(hw::RouterApiError(113004)) == ExitCode::router);
    CHECK(exit_code_for(hw::RouterError("xml_response_not_ok", "")) == ExitCode::router);
    CHECK(exit_code_for(hw::RouterError("invalid_response", "")) == ExitCode::router);
}

TEST_CASE("Other exceptions map to failure", "[exit_codes]") {
    CHECK(exit_code_for(std::runtime_error("x")) == ExitCode::failure);
    CHECK(exit_code_for(std::logic_error("x")) == ExitCode::failure);
}

TEST_CASE("error_code_for returns the RouterError code", "[exit_codes]") {
    CHECK(error_code_for(hw::RequestError("timeout", "")) == "http_request_timeout");
    CHECK(error_code_for(hw::RouterApiError(108002)) == "api_login_password_wrong");
    CHECK(error_code_for(hw::RouterError("invalid_router_url", "x")) == "invalid_router_url");
}

TEST_CASE("error_code_for names invalid_argument and other exceptions", "[exit_codes]") {
    CHECK(error_code_for(std::invalid_argument("bad")) == "invalid_argument");
    CHECK(error_code_for(std::runtime_error("x")) == "internal");
    CHECK(error_code_for(std::logic_error("x")) == "internal");
}
