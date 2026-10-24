#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <optional>
#include <string>
#include <string_view>

#include "hw/api/admin.hpp"
#include "hw/errors.hpp"
#include "hw/options.hpp"
#include "hw/session.hpp"
#include "hw/transport.hpp"
#include "hw/util/crypto.hpp"
#include "support/fake_transport.hpp"

namespace {

constexpr std::string_view kTokenHeader = "__RequestVerificationToken";
constexpr std::string_view kDecl = R"(<?xml version="1.0" encoding="UTF-8"?>)";
constexpr std::string_view kHomeToken = "TOKEN0123456789abcdef0123456789ab";
constexpr std::string_view kPassword4 =
    "MTA2Y2U1MzgwNTE1OTcyOTk0MGZlZmU1ODU2ZTU3MDRkN2UwMDVhODhjOTU2ODQ3YzY0OWQ1Y2RmYTEyMWMyMA==";
constexpr std::string_view kPasswordBase64 = "c2VjcmV0";  // base64("secret")

// Value of the named request header, or nullopt if it is absent.
std::optional<std::string> header_of(const hw::HttpRequest& request, std::string_view name) {
    const auto it = request.headers.find(name);
    if (it == request.headers.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<std::string> token_of(const hw::HttpRequest& request) {
    return header_of(request, kTokenHeader);
}

hw::Credentials admin_secret() {
    hw::Credentials credentials;
    credentials.username = "admin";
    credentials.password = "secret";
    return credentials;
}

// Full login body as Admin must send it: Username, Password, password_type in that order.
std::string login_body(std::string_view username, std::string_view password,
                       int password_type) {
    return std::string(kDecl) + "<request><Username>" + std::string(username) +
           "</Username><Password>" +
           std::string(password) + "</Password><password_type>" +
           std::to_string(password_type) + "</password_type></request>";
}

// Successful state-login response.
hw::HttpResponse state_response(int state, std::string_view username, int password_type) {
    return FakeTransport::xml("<response><State>" + std::to_string(state) + "</State><Username>" +
                              std::string(username) + "</Username><password_type>" +
                              std::to_string(password_type) + "</password_type></response>");
}

hw::HttpResponse ok_response() {
    return FakeTransport::xml("<response>OK</response>");
}

// Runs fn and returns a copy of the exception of type E it threw, or nullopt if it threw none.
template <typename E, typename Fn>
std::optional<E> thrown(Fn&& fn) {
    try {
        fn();
    } catch (const E& e) {
        return e;
    }
    return std::nullopt;
}

}  // namespace

TEST_CASE("state() sends GET api/user/state-login and parses the fields", "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(state_response(0, "admin", 4));

    hw::Session session(transport);
    hw::api::Admin admin(session);
    const auto state = admin.state();

    CHECK(state.state == 0);
    CHECK(state.username == "admin");
    CHECK(state.password_type == 4);

    REQUIRE(transport.requests.size() == 2);
    CHECK(transport.requests[0].method == "GET");
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(transport.requests[1].method == "GET");
    CHECK(transport.requests[1].path == "api/user/state-login");
    CHECK(token_of(transport.requests[1]) == std::string(kHomeToken));
    CHECK(transport.requests[1].body.empty());
}

TEST_CASE("state() with a missing password_type defaults it to 0", "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(FakeTransport::xml("<response><State>1</State>"
                                                  "<Username>admin</Username></response>"));

    hw::Session session(transport);
    hw::api::Admin admin(session);
    const auto state = admin.state();

    CHECK(state.state == 1);
    CHECK(state.username == "admin");
    CHECK(state.password_type == 0);
    REQUIRE(transport.requests.size() == 2);
}

TEST_CASE("state() accepts an empty Username", "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(state_response(1, "", 4));

    hw::Session session(transport);
    hw::api::Admin admin(session);
    const auto state = admin.state();

    CHECK(state.state == 1);
    CHECK(state.username.empty());
    CHECK(state.password_type == 4);
    REQUIRE(transport.requests.size() == 2);
}

TEST_CASE("state() with a missing State element throws xml_missing_field", "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(FakeTransport::xml("<response><Username>admin</Username>"
                                                  "<password_type>4</password_type></response>"));

    hw::Session session(transport);
    hw::api::Admin admin(session);
    const auto error = thrown<hw::RouterError>([&] { admin.state(); });

    REQUIRE(error.has_value());
    CHECK(error->code() == "xml_missing_field");
    REQUIRE(transport.requests.size() == 2);
}

TEST_CASE("state() rejects a State that is not a strict integer", "[admin]") {
    const auto value = GENERATE(as<std::string>{}, "1x", "x1", "1.5", "99999999999");
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(FakeTransport::xml("<response><State>" + value +
                                                  "</State><password_type>4</password_type>"
                                                  "</response>"));

    hw::Session session(transport);
    hw::api::Admin admin(session);
    const auto error = thrown<hw::RouterError>([&] { admin.state(); });

    REQUIRE(error.has_value());
    CHECK(error->code() == "invalid_response");
    REQUIRE(transport.requests.size() == 2);
}

TEST_CASE("state() with an empty State element throws invalid_response", "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(FakeTransport::xml("<response><State></State>"
                                                  "<password_type>4</password_type></response>"));

    hw::Session session(transport);
    hw::api::Admin admin(session);
    const auto error = thrown<hw::RouterError>([&] { admin.state(); });

    REQUIRE(error.has_value());
    CHECK(error->code() == "invalid_response");
    REQUIRE(transport.requests.size() == 2);
}

TEST_CASE("state() rejects a password_type that is not a strict integer", "[admin]") {
    const auto value = GENERATE(as<std::string>{}, "1x", "x1", "1.5", "99999999999");
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(FakeTransport::xml("<response><State>1</State>"
                                                  "<password_type>" + value +
                                                  "</password_type></response>"));

    hw::Session session(transport);
    hw::api::Admin admin(session);
    const auto error = thrown<hw::RouterError>([&] { admin.state(); });

    REQUIRE(error.has_value());
    CHECK(error->code() == "invalid_response");
    REQUIRE(transport.requests.size() == 2);
}

TEST_CASE("state() reads an empty password_type element as 0", "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(FakeTransport::xml("<response><State>1</State>"
                                                  "<password_type></password_type></response>"));

    hw::Session session(transport);
    hw::api::Admin admin(session);
    const auto state = admin.state();

    CHECK(state.state == 1);
    CHECK(state.password_type == 0);
    REQUIRE(transport.requests.size() == 2);
}

TEST_CASE("is_logged_in() is true only when the state is 0", "[admin]") {
    SECTION("logged in") {
        FakeTransport transport;
        transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
        transport.script.push_back(state_response(0, "admin", 4));

        hw::Session session(transport);
        hw::api::Admin admin(session);
        CHECK(admin.is_logged_in());
        REQUIRE(transport.requests.size() == 2);
        CHECK(transport.requests[1].path == "api/user/state-login");
    }
    SECTION("logged out") {
        for (const int value : {1, 2, -1}) {
            FakeTransport transport;
            transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
            transport.script.push_back(state_response(value, "admin", 4));

            hw::Session session(transport);
            hw::api::Admin admin(session);
            CHECK_FALSE(admin.is_logged_in());
            REQUIRE(transport.requests.size() == 2);
            CHECK(transport.requests[1].path == "api/user/state-login");
        }
    }
}

TEST_CASE("login() refreshes tokens, reads state and posts the SHA-256 password", "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(state_response(1, "admin", 4));
    transport.script.push_back(ok_response());

    hw::Session session(transport);
    hw::api::Admin admin(session);
    admin.login(admin_secret());

    REQUIRE(transport.requests.size() == 3);

    CHECK(transport.requests[0].method == "GET");
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(token_of(transport.requests[0]) == std::nullopt);

    CHECK(transport.requests[1].method == "GET");
    CHECK(transport.requests[1].path == "api/user/state-login");
    CHECK(token_of(transport.requests[1]) == std::string(kHomeToken));

    CHECK(transport.requests[2].method == "POST");
    CHECK(transport.requests[2].path == "api/user/login");
    CHECK(token_of(transport.requests[2]) == std::string(kHomeToken));
    CHECK(transport.requests[2].body == login_body("admin", kPassword4, 4));
}

TEST_CASE("login() with password_type 0 posts the base64 password and the front token",
          "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(state_response(1, "admin", 0));
    transport.script.push_back(ok_response());

    hw::Session session(transport);
    hw::api::Admin admin(session);
    admin.login(admin_secret());

    REQUIRE(transport.requests.size() == 3);
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(transport.requests[1].method == "GET");
    CHECK(transport.requests[1].path == "api/user/state-login");
    CHECK(transport.requests[2].method == "POST");
    CHECK(transport.requests[2].path == "api/user/login");
    CHECK(token_of(transport.requests[2]) == std::string(kHomeToken));
    CHECK(transport.requests[2].body == login_body("admin", kPasswordBase64, 0));
}

TEST_CASE("login() with password_type 4 and an empty token pool falls back to base64",
          "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::xml("<html><head></head><body></body></html>"));
    transport.script.push_back(FakeTransport::error(100002));
    transport.script.push_back(state_response(1, "admin", 4));
    transport.script.push_back(ok_response());

    hw::Session session(transport);
    hw::api::Admin admin(session);
    admin.login(admin_secret());

    REQUIRE(transport.requests.size() == 4);
    CHECK(transport.requests[0].method == "GET");
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(transport.requests[1].method == "GET");
    CHECK(transport.requests[1].path == "api/webserver/token");
    CHECK(token_of(transport.requests[1]) == std::nullopt);
    CHECK(transport.requests[2].method == "GET");
    CHECK(transport.requests[2].path == "api/user/state-login");
    CHECK(token_of(transport.requests[2]) == std::nullopt);
    CHECK(transport.requests[3].method == "POST");
    CHECK(transport.requests[3].path == "api/user/login");
    CHECK(token_of(transport.requests[3]) == std::nullopt);
    CHECK(transport.requests[3].body == login_body("admin", kPasswordBase64, 4));
}

TEST_CASE("login() hashes with the freshly fetched token, not a stale pool", "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"stale"}));
    transport.script.push_back(FakeTransport::home_page({"fresh"}));
    transport.script.push_back(state_response(1, "admin", 4));
    transport.script.push_back(ok_response());

    hw::Session session(transport);
    session.refresh_tokens();
    hw::api::Admin admin(session);
    admin.login(admin_secret());

    REQUIRE(transport.requests.size() == 4);
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(transport.requests[1].path == "html/home.html");
    CHECK(transport.requests[2].method == "GET");
    CHECK(transport.requests[2].path == "api/user/state-login");
    CHECK(token_of(transport.requests[2]) == "fresh");
    CHECK(transport.requests[3].method == "POST");
    CHECK(transport.requests[3].path == "api/user/login");
    CHECK(token_of(transport.requests[3]) == "fresh");
    const std::string password = hw::util::encode_login_password("admin", "secret", "fresh");
    CHECK(transport.requests[3].body == login_body("admin", password, 4));
}

TEST_CASE("login() propagates a router error such as 108002 as an auth error", "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(state_response(1, "admin", 4));
    transport.script.push_back(FakeTransport::error(108002));

    hw::Session session(transport);
    hw::api::Admin admin(session);
    const auto error = thrown<hw::RouterApiError>([&] { admin.login(admin_secret()); });

    REQUIRE(error.has_value());
    CHECK(error->api_code() == 108002);
    CHECK(hw::is_auth_error(*error));
    REQUIRE(transport.requests.size() == 3);
    CHECK(transport.requests[2].path == "api/user/login");
}

TEST_CASE("ensure_logged_in() logs in when logged out, with one state call and no refresh",
          "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(state_response(1, "admin", 4));
    transport.script.push_back(ok_response());

    hw::Session session(transport);
    hw::api::Admin admin(session);
    admin.ensure_logged_in(admin_secret());

    REQUIRE(transport.requests.size() == 3);
    CHECK(transport.requests[0].method == "GET");
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(transport.requests[1].method == "GET");
    CHECK(transport.requests[1].path == "api/user/state-login");
    CHECK(token_of(transport.requests[1]) == std::string(kHomeToken));
    CHECK(transport.requests[2].method == "POST");
    CHECK(transport.requests[2].path == "api/user/login");
    CHECK(token_of(transport.requests[2]) == std::string(kHomeToken));
    CHECK(transport.requests[2].body == login_body("admin", kPassword4, 4));
}

TEST_CASE("ensure_logged_in() does not POST when already logged in", "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(state_response(0, "admin", 4));

    hw::Session session(transport);
    hw::api::Admin admin(session);
    admin.ensure_logged_in(admin_secret());

    REQUIRE(transport.requests.size() == 2);
    CHECK(transport.requests[0].method == "GET");
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(transport.requests[1].method == "GET");
    CHECK(transport.requests[1].path == "api/user/state-login");
    CHECK(token_of(transport.requests[1]) == std::string(kHomeToken));
}

TEST_CASE("login() hashes with the front token when the pool holds several", "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"t1", "t2"}));
    transport.script.push_back(state_response(1, "admin", 4));
    transport.script.push_back(ok_response());

    hw::Session session(transport);
    hw::api::Admin admin(session);
    admin.login(admin_secret());

    REQUIRE(transport.requests.size() == 3);
    CHECK(token_of(transport.requests[1]) == "t1");
    CHECK(token_of(transport.requests[2]) == "t1");
    CHECK(transport.requests[2].body ==
          login_body("admin", hw::util::encode_login_password("admin", "secret", "t1"), 4));
}

TEST_CASE("ensure_logged_in() hashes with the front token when the pool holds several",
          "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"t1", "t2"}));
    transport.script.push_back(state_response(1, "admin", 4));
    transport.script.push_back(ok_response());

    hw::Session session(transport);
    hw::api::Admin admin(session);
    admin.ensure_logged_in(admin_secret());

    REQUIRE(transport.requests.size() == 3);
    CHECK(token_of(transport.requests[1]) == "t1");
    CHECK(token_of(transport.requests[2]) == "t1");
    CHECK(transport.requests[2].body ==
          login_body("admin", hw::util::encode_login_password("admin", "secret", "t1"), 4));
}

TEST_CASE("ensure_logged_in() with password_type 0 posts base64 with the front token", "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(state_response(1, "admin", 0));
    transport.script.push_back(ok_response());

    hw::Session session(transport);
    hw::api::Admin admin(session);
    admin.ensure_logged_in(admin_secret());

    REQUIRE(transport.requests.size() == 3);
    CHECK(transport.requests[1].path == "api/user/state-login");
    CHECK(transport.requests[2].method == "POST");
    CHECK(transport.requests[2].path == "api/user/login");
    CHECK(token_of(transport.requests[2]) == std::string(kHomeToken));
    CHECK(transport.requests[2].body == login_body("admin", kPasswordBase64, 0));
}

TEST_CASE("ensure_logged_in() with an empty token pool posts base64 without a token",
          "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::xml("<html><head></head><body></body></html>"));
    transport.script.push_back(FakeTransport::error(100002));
    transport.script.push_back(state_response(1, "admin", 4));
    transport.script.push_back(ok_response());

    hw::Session session(transport);
    hw::api::Admin admin(session);
    admin.ensure_logged_in(admin_secret());

    REQUIRE(transport.requests.size() == 4);
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(transport.requests[1].path == "api/webserver/token");
    CHECK(transport.requests[2].method == "GET");
    CHECK(transport.requests[2].path == "api/user/state-login");
    CHECK(token_of(transport.requests[2]) == std::nullopt);
    CHECK(transport.requests[3].method == "POST");
    CHECK(transport.requests[3].path == "api/user/login");
    CHECK(token_of(transport.requests[3]) == std::nullopt);
    CHECK(transport.requests[3].body == login_body("admin", kPasswordBase64, 4));
}

TEST_CASE("ensure_logged_in() propagates a state router error without posting", "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(FakeTransport::error(100002));

    hw::Session session(transport);
    hw::api::Admin admin(session);
    const auto error = thrown<hw::RouterApiError>([&] { admin.ensure_logged_in(admin_secret()); });

    REQUIRE(error.has_value());
    CHECK(error->api_code() == 100002);
    REQUIRE(transport.requests.size() == 2);
    CHECK(transport.requests[1].method == "GET");
    CHECK(transport.requests[1].path == "api/user/state-login");
}

TEST_CASE("login() posts even when state() already reports logged in", "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(state_response(0, "admin", 4));
    transport.script.push_back(ok_response());

    hw::Session session(transport);
    hw::api::Admin admin(session);
    admin.login(admin_secret());

    REQUIRE(transport.requests.size() == 3);
    CHECK(transport.requests[1].path == "api/user/state-login");
    CHECK(transport.requests[2].method == "POST");
    CHECK(transport.requests[2].path == "api/user/login");
    CHECK(transport.requests[2].body == login_body("admin", kPassword4, 4));
}

TEST_CASE("login() uses the username from the credentials, not the router state", "[admin]") {
    hw::Credentials credentials;
    credentials.username = "root";
    credentials.password = "secret";

    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(state_response(1, "admin", 4));
    transport.script.push_back(ok_response());

    hw::Session session(transport);
    hw::api::Admin admin(session);
    admin.login(credentials);

    REQUIRE(transport.requests.size() == 3);
    CHECK(transport.requests[2].path == "api/user/login");
    CHECK(transport.requests[2].body ==
          login_body("root", hw::util::encode_login_password("root", "secret", kHomeToken), 4));
}

TEST_CASE("ensure_logged_in() posts for a state other than 0 or 1", "[admin]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(state_response(2, "admin", 4));
    transport.script.push_back(ok_response());

    hw::Session session(transport);
    hw::api::Admin admin(session);
    admin.ensure_logged_in(admin_secret());

    REQUIRE(transport.requests.size() == 3);
    CHECK(transport.requests[2].method == "POST");
    CHECK(transport.requests[2].path == "api/user/login");
    CHECK(transport.requests[2].body == login_body("admin", kPassword4, 4));
}
