#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hw/errors.hpp"
#include "hw/session.hpp"
#include "hw/transport.hpp"
#include "hw/xml.hpp"
#include "support/fake_transport.hpp"

namespace {

constexpr std::string_view kTokenHeader = "__RequestVerificationToken";
constexpr std::string_view kContentType = "application/x-www-form-urlencoded; charset=UTF-8";
constexpr std::string_view kOk = "<response>OK</response>";
constexpr std::string_view kNotOk = "<response>FAIL</response>";
constexpr std::string_view kJunkPrefix = "0123456789abcdef0123456789abcdef";
constexpr std::string_view kRealToken32 = "fedcba9876543210fedcba9876543210";

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

std::vector<std::string> tokens_of(const hw::Session& session) {
    return {session.tokens().begin(), session.tokens().end()};
}

hw::XmlRequest sample_body() {
    hw::XmlRequest body;
    body.add("Index", 1LL);
    return body;
}

// Body whose <v> element carries value, so tests can tell which response they got.
std::string value_body(std::string_view value) {
    return "<response><v>" + std::string(value) + "</v></response>";
}

hw::HttpResponse ok(std::string_view value, hw::Headers headers = {}) {
    return FakeTransport::xml(value_body(value), std::move(headers));
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

TEST_CASE("First get loads tokens once, does not consume them, and sends no Content-Type",
          "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    // GET responses do not rotate: the zz/z1 headers must be ignored.
    transport.script.push_back(ok("1", hw::Headers{{"__RequestVerificationToken", "zz"},
                                                   {"__RequestVerificationTokenOne", "z1"}}));
    transport.script.push_back(ok("2"));

    hw::Session session(transport);
    const auto first = session.get("api/a");
    const auto second = session.get("api/b");

    CHECK(first.root().text("v") == "1");
    CHECK(second.root().text("v") == "2");

    REQUIRE(transport.requests.size() == 3);
    CHECK(transport.requests[0].method == "GET");
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(token_of(transport.requests[0]) == std::nullopt);
    CHECK(header_of(transport.requests[0], "Content-Type") == std::nullopt);

    CHECK(transport.requests[1].method == "GET");
    CHECK(transport.requests[1].path == "api/a");
    CHECK(token_of(transport.requests[1]) == "a");
    CHECK(header_of(transport.requests[1], "Content-Type") == std::nullopt);

    CHECK(transport.requests[2].method == "GET");
    CHECK(transport.requests[2].path == "api/b");
    CHECK(token_of(transport.requests[2]) == "a");

    CHECK(tokens_of(session) == std::vector<std::string>{"a"});
}

TEST_CASE("Meta tags are collected in document order whatever their attribute form", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::xml(
        "<html><head>"
        "<meta content=\"t1\" name=\"csrf_token\">"
        "<meta name='csrf_token' content='t2'/>"
        "<meta name=\"csrf_token\" content=\"t3\" />"
        "<meta name=\"other\" content=\"nope\">"
        "</head></html>"));
    transport.script.push_back(ok("1"));

    hw::Session session(transport);
    const auto response = session.get("api/x");

    CHECK(response.root().text("v") == "1");
    REQUIRE(transport.requests.size() == 2);
    CHECK(token_of(transport.requests[1]) == "t1");
    CHECK(tokens_of(session) == std::vector<std::string>{"t1", "t2", "t3"});
}

TEST_CASE("Fallback to api/webserver/token drops the first 32 characters of a longer token",
          "[session]") {
    const std::string token = std::string(kJunkPrefix) + std::string(kRealToken32);
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({}));
    transport.script.push_back(FakeTransport::xml("<response><token>" + token +
                                                  "</token></response>"));
    transport.script.push_back(ok("1"));

    hw::Session session(transport);
    const auto response = session.get("api/x");

    CHECK(response.root().text("v") == "1");
    REQUIRE(transport.requests.size() == 3);
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(token_of(transport.requests[0]) == std::nullopt);
    CHECK(transport.requests[1].method == "GET");
    CHECK(transport.requests[1].path == "api/webserver/token");
    CHECK(token_of(transport.requests[1]) == std::nullopt);
    CHECK(transport.requests[2].path == "api/x");
    CHECK(token_of(transport.requests[2]) == std::string(kRealToken32));
    CHECK(tokens_of(session) == std::vector<std::string>{std::string(kRealToken32)});
}

TEST_CASE("Fallback token of 40 characters keeps only the characters after the first 32",
          "[session]") {
    const std::string token = std::string(32, 'J') + "ABCDEFGH";
    REQUIRE(token.size() == 40);
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({}));
    transport.script.push_back(FakeTransport::xml("<response><token>" + token +
                                                  "</token></response>"));
    transport.script.push_back(ok("1"));

    hw::Session session(transport);
    const auto response = session.get("api/x");

    CHECK(response.root().text("v") == "1");
    REQUIRE(transport.requests.size() == 3);
    CHECK(token_of(transport.requests[2]) == "ABCDEFGH");
    CHECK(tokens_of(session) == std::vector<std::string>{"ABCDEFGH"});
}

TEST_CASE("Fallback to api/webserver/token keeps a 32-character token whole", "[session]") {
    const std::string token = "ABCDEFGHIJKLMNOPQRSTUVWXYZ123456";
    REQUIRE(token.size() == 32);
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({}));
    transport.script.push_back(FakeTransport::xml("<response><token>" + token +
                                                  "</token></response>"));
    transport.script.push_back(ok("1"));

    hw::Session session(transport);
    const auto response = session.get("api/x");

    CHECK(response.root().text("v") == "1");
    REQUIRE(transport.requests.size() == 3);
    CHECK(token_of(transport.requests[1]) == std::nullopt);
    CHECK(token_of(transport.requests[2]) == token);
    CHECK(tokens_of(session) == std::vector<std::string>{token});
}

TEST_CASE("Router error from the fallback endpoint leaves tokens empty and is not an error",
          "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({}));
    transport.script.push_back(FakeTransport::error(100002));
    transport.script.push_back(ok("1"));
    transport.script.push_back(ok("2"));

    hw::Session session(transport);
    const auto first = session.get("api/x");
    const auto second = session.get("api/y");

    CHECK(first.root().text("v") == "1");
    CHECK(second.root().text("v") == "2");
    // The empty-but-loaded pool must not trigger another home.html or fallback request.
    REQUIRE(transport.requests.size() == 4);
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(transport.requests[1].path == "api/webserver/token");
    CHECK(token_of(transport.requests[1]) == std::nullopt);
    CHECK(transport.requests[2].path == "api/x");
    CHECK(token_of(transport.requests[2]) == std::nullopt);
    CHECK(transport.requests[3].path == "api/y");
    CHECK(token_of(transport.requests[3]) == std::nullopt);
    CHECK(tokens_of(session).empty());
}

TEST_CASE("Fallback token error does not throw, leaves tokens empty and does not reload",
          "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({}));
    transport.script.push_back(FakeTransport::error(125001));
    transport.script.push_back(ok("1"));
    transport.script.push_back(ok("2"));

    hw::Session session(transport);
    CHECK_NOTHROW(session.refresh_tokens());
    CHECK(tokens_of(session).empty());

    const auto first = session.get("api/x");
    const auto second = session.get("api/y");

    CHECK(first.root().text("v") == "1");
    CHECK(second.root().text("v") == "2");
    REQUIRE(transport.requests.size() == 4);
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(transport.requests[1].path == "api/webserver/token");
    CHECK(token_of(transport.requests[2]) == std::nullopt);
    CHECK(token_of(transport.requests[3]) == std::nullopt);
    CHECK(tokens_of(session).empty());
}

TEST_CASE("Fallback token request sends no token header even when the pool is non-empty",
          "[session]") {
    const std::string fallback(32, 'K');
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(FakeTransport::error(125001));
    transport.script.push_back(FakeTransport::home_page({}));
    transport.script.push_back(FakeTransport::xml("<response><token>" + fallback +
                                                  "</token></response>"));
    transport.script.push_back(ok("1"));

    hw::Session session(transport);
    const auto response = session.get("api/x");

    CHECK(response.root().text("v") == "1");
    REQUIRE(transport.requests.size() == 5);
    CHECK(token_of(transport.requests[0]) == std::nullopt);
    CHECK(token_of(transport.requests[1]) == "a");
    CHECK(token_of(transport.requests[2]) == std::nullopt);
    CHECK(transport.requests[3].path == "api/webserver/token");
    CHECK(token_of(transport.requests[3]) == std::nullopt);
    CHECK(token_of(transport.requests[4]) == fallback);
    CHECK(tokens_of(session) == std::vector<std::string>{fallback});
}

TEST_CASE("RequestError from home.html propagates out of refresh_tokens", "[session]") {
    FakeTransport transport;
    transport.script.push_back(hw::RequestError("timeout", "boom"));

    hw::Session session(transport);
    const auto error = thrown<hw::RequestError>([&] { session.refresh_tokens(); });

    REQUIRE(error.has_value());
    CHECK(error->code() == "http_request_timeout");
    CHECK(transport.requests.size() == 1);
    CHECK(tokens_of(session).empty());
}

TEST_CASE("Fallback body without a token element throws xml_missing_field", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({}));
    transport.script.push_back(FakeTransport::xml("<response></response>"));

    hw::Session session(transport);
    const auto error = thrown<hw::RouterError>([&] { session.refresh_tokens(); });

    REQUIRE(error.has_value());
    CHECK(error->code() == "xml_missing_field");
    CHECK(transport.requests.size() == 2);
}

TEST_CASE("A failed lazy load is retried by the next get", "[session]") {
    FakeTransport transport;
    transport.script.push_back(hw::RequestError("timeout", "boom"));
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(ok("1"));

    hw::Session session(transport);
    const auto error = thrown<hw::RequestError>([&] { session.get("api/x"); });
    REQUIRE(error.has_value());

    const auto response = session.get("api/x");
    CHECK(response.root().text("v") == "1");

    REQUIRE(transport.requests.size() == 3);
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(transport.requests[1].path == "html/home.html");
    CHECK(transport.requests[2].path == "api/x");
    CHECK(token_of(transport.requests[2]) == "a");
    CHECK(tokens_of(session) == std::vector<std::string>{"a"});
}

TEST_CASE("Post sends the front token with the form body and consumes it", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a", "b"}));
    transport.script.push_back(ok("1"));

    const auto body = sample_body();
    hw::Session session(transport);
    const auto response = session.post("api/set", body);

    CHECK(response.root().text("v") == "1");
    REQUIRE(transport.requests.size() == 2);
    CHECK(transport.requests[0].method == "GET");
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(token_of(transport.requests[0]) == std::nullopt);

    const auto& post = transport.requests[1];
    CHECK(post.method == "POST");
    CHECK(post.path == "api/set");
    CHECK(post.body == body.str());
    CHECK(header_of(post, "Content-Type") == std::string(kContentType));
    CHECK(token_of(post) == "a");

    CHECK(tokens_of(session) == std::vector<std::string>{"b"});
}

TEST_CASE("Post sends no token header once the pool is empty", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(ok("1"));
    transport.script.push_back(ok("2"));

    hw::Session session(transport);
    const auto first = session.post("api/one", sample_body());
    const auto second = session.post("api/two", sample_body());

    CHECK(first.root().text("v") == "1");
    CHECK(second.root().text("v") == "2");
    REQUIRE(transport.requests.size() == 3);
    CHECK(token_of(transport.requests[1]) == "a");
    CHECK(token_of(transport.requests[2]) == std::nullopt);
    CHECK(header_of(transport.requests[2], "Content-Type") == std::string(kContentType));
    CHECK(tokens_of(session).empty());
}

TEST_CASE("Post appends One and Two response tokens in that order", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(ok("1", hw::Headers{{"__RequestVerificationTokenOne", "n1"},
                                                   {"__RequestVerificationTokenTwo", "n2"}}));

    hw::Session session(transport);
    CHECK(session.post("api/set", sample_body()).root().text("v") == "1");

    CHECK(tokens_of(session) == std::vector<std::string>{"n1", "n2"});
}

TEST_CASE("Post appends a single response token", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(ok("1", hw::Headers{{"__RequestVerificationToken", "n"}}));

    hw::Session session(transport);
    CHECK(session.post("api/set", sample_body()).root().text("v") == "1");

    CHECK(tokens_of(session) == std::vector<std::string>{"n"});
}

TEST_CASE("Post without rotation headers appends nothing", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a", "b"}));
    transport.script.push_back(ok("1"));

    hw::Session session(transport);
    CHECK(session.post("api/set", sample_body()).root().text("v") == "1");

    CHECK(tokens_of(session) == std::vector<std::string>{"b"});
}

TEST_CASE("Two without One is ignored", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a", "b"}));
    transport.script.push_back(ok("1", hw::Headers{{"__RequestVerificationTokenTwo", "n2"}}));

    hw::Session session(transport);
    CHECK(session.post("api/set", sample_body()).root().text("v") == "1");

    CHECK(tokens_of(session) == std::vector<std::string>{"b"});
}

TEST_CASE("One takes precedence over the single token header", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(ok("1", hw::Headers{{"__RequestVerificationToken", "s"},
                                                   {"__RequestVerificationTokenOne", "n1"}}));

    hw::Session session(transport);
    CHECK(session.post("api/set", sample_body()).root().text("v") == "1");

    CHECK(tokens_of(session) == std::vector<std::string>{"n1"});
}

TEST_CASE("Single token header is ignored when One and Two are present", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(ok("1", hw::Headers{{"__RequestVerificationToken", "s"},
                                                   {"__RequestVerificationTokenOne", "n1"},
                                                   {"__RequestVerificationTokenTwo", "n2"}}));

    hw::Session session(transport);
    CHECK(session.post("api/set", sample_body()).root().text("v") == "1");

    CHECK(tokens_of(session) == std::vector<std::string>{"n1", "n2"});
}

TEST_CASE("Rotation header names are matched case-insensitively", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(ok("1", hw::Headers{{"__requestverificationtokenone", "n1"},
                                                   {"__REQUESTVERIFICATIONTOKENTWO", "n2"}}));

    hw::Session session(transport);
    CHECK(session.post("api/set", sample_body()).root().text("v") == "1");

    CHECK(tokens_of(session) == std::vector<std::string>{"n1", "n2"});
}

TEST_CASE("Successful login clears remaining tokens before appending the new one", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a", "b"}));
    transport.script.push_back(ok("1", hw::Headers{{"__RequestVerificationTokenOne", "n1"}}));

    hw::Session session(transport);
    CHECK(session.post("api/user/login", sample_body()).root().text("v") == "1");

    REQUIRE(transport.requests.size() == 2);
    CHECK(transport.requests[1].path == "api/user/login");
    CHECK(token_of(transport.requests[1]) == "a");
    CHECK(tokens_of(session) == std::vector<std::string>{"n1"});
}

TEST_CASE("Login path with a suffix does not clear remaining tokens", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a", "b"}));
    transport.script.push_back(ok("1"));

    hw::Session session(transport);
    CHECK(session.post("api/user/login/x", sample_body()).root().text("v") == "1");

    REQUIRE(transport.requests.size() == 2);
    CHECK(transport.requests[1].path == "api/user/login/x");
    CHECK(tokens_of(session) == std::vector<std::string>{"b"});
}

TEST_CASE("Logout does not clear remaining tokens", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a", "b"}));
    transport.script.push_back(ok("1", hw::Headers{{"__RequestVerificationTokenOne", "n1"}}));

    hw::Session session(transport);
    CHECK(session.post("api/user/logout", sample_body()).root().text("v") == "1");

    REQUIRE(transport.requests.size() == 2);
    CHECK(transport.requests[1].path == "api/user/logout");
    CHECK(tokens_of(session) == std::vector<std::string>{"b", "n1"});
}

TEST_CASE("Login router error keeps remaining tokens and still appends the response token",
          "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a", "b"}));
    transport.script.push_back(FakeTransport::xml(
        "<error><code>108002</code><message></message></error>",
        hw::Headers{{"__RequestVerificationToken", "n"}}));

    hw::Session session(transport);
    const auto error = thrown<hw::RouterApiError>(
        [&] { session.post("api/user/login", sample_body()); });

    REQUIRE(error.has_value());
    CHECK(error->api_code() == 108002);
    REQUIRE(transport.requests.size() == 2);
    CHECK(token_of(transport.requests[1]) == "a");
    CHECK(tokens_of(session) == std::vector<std::string>{"b", "n"});
}

TEST_CASE("Token error on get reloads tokens and retries the get once, for each token code",
          "[session]") {
    const int code = GENERATE(125001, 125002, 125003);
    CAPTURE(code);
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(FakeTransport::error(code));
    transport.script.push_back(FakeTransport::home_page({"b"}));
    transport.script.push_back(ok("2"));

    hw::Session session(transport);
    const auto response = session.get("api/x");

    CHECK(response.root().text("v") == "2");
    REQUIRE(transport.requests.size() == 4);
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(token_of(transport.requests[0]) == std::nullopt);
    CHECK(transport.requests[1].path == "api/x");
    CHECK(token_of(transport.requests[1]) == "a");
    CHECK(transport.requests[2].path == "html/home.html");
    CHECK(token_of(transport.requests[2]) == std::nullopt);
    CHECK(transport.requests[3].method == "GET");
    CHECK(transport.requests[3].path == "api/x");
    CHECK(token_of(transport.requests[3]) == "b");
    CHECK(tokens_of(session) == std::vector<std::string>{"b"});
}

TEST_CASE("Token error on post reloads tokens and retries the post once", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a", "b"}));
    transport.script.push_back(FakeTransport::error(125002));
    transport.script.push_back(FakeTransport::home_page({"c"}));
    transport.script.push_back(ok("3"));

    const auto body = sample_body();
    hw::Session session(transport);
    const auto response = session.post("api/set", body);

    CHECK(response.root().text("v") == "3");
    REQUIRE(transport.requests.size() == 4);
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(transport.requests[1].method == "POST");
    CHECK(transport.requests[1].path == "api/set");
    CHECK(transport.requests[1].body == body.str());
    CHECK(token_of(transport.requests[1]) == "a");
    CHECK(transport.requests[2].path == "html/home.html");
    CHECK(token_of(transport.requests[2]) == std::nullopt);
    CHECK(transport.requests[3].method == "POST");
    CHECK(transport.requests[3].path == "api/set");
    CHECK(transport.requests[3].body == body.str());
    CHECK(header_of(transport.requests[3], "Content-Type") == std::string(kContentType));
    CHECK(token_of(transport.requests[3]) == "c");
    CHECK(tokens_of(session).empty());
}

TEST_CASE("Retried post with Expect::Ok still enforces OK", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(FakeTransport::error(125001));
    transport.script.push_back(FakeTransport::home_page({"b"}));
    transport.script.push_back(FakeTransport::xml(std::string(kNotOk)));

    hw::Session session(transport);
    const auto error = thrown<hw::RouterError>(
        [&] { session.post("api/set", sample_body(), hw::Expect::Ok); });

    REQUIRE(error.has_value());
    CHECK(error->code() == "xml_response_not_ok");
    CHECK(transport.requests.size() == 4);
    CHECK(token_of(transport.requests[3]) == "b");
}

TEST_CASE("Second consecutive token error on get propagates after one retry", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(FakeTransport::error(125001));
    transport.script.push_back(FakeTransport::home_page({"b"}));
    transport.script.push_back(FakeTransport::error(125003));

    hw::Session session(transport);
    const auto error = thrown<hw::RouterApiError>([&] { session.get("api/x"); });

    REQUIRE(error.has_value());
    CHECK(error->api_code() == 125003);
    CHECK(transport.requests.size() == 4);
}

TEST_CASE("Second consecutive token error on post propagates after one retry", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(FakeTransport::error(125001));
    transport.script.push_back(FakeTransport::home_page({"b"}));
    transport.script.push_back(FakeTransport::error(125001));

    hw::Session session(transport);
    const auto error = thrown<hw::RouterApiError>(
        [&] { session.post("api/set", sample_body()); });

    REQUIRE(error.has_value());
    CHECK(error->api_code() == 125001);
    CHECK(transport.requests.size() == 4);
}

TEST_CASE("Token error 125004 is not a token error and is not retried", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(FakeTransport::error(125004));

    hw::Session session(transport);
    const auto error = thrown<hw::RouterApiError>([&] { session.get("api/x"); });

    REQUIRE(error.has_value());
    CHECK(error->api_code() == 125004);
    CHECK(transport.requests.size() == 2);
}

TEST_CASE("Non-token router error is not retried", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(FakeTransport::error(108002));

    hw::Session session(transport);
    const auto error = thrown<hw::RouterApiError>([&] { session.get("api/x"); });

    REQUIRE(error.has_value());
    CHECK(error->api_code() == 108002);
    CHECK(transport.requests.size() == 2);
}

TEST_CASE("Transport RequestError on get propagates without retry", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(hw::RequestError("timeout", "boom"));

    hw::Session session(transport);
    const auto error = thrown<hw::RequestError>([&] { session.get("api/x"); });

    REQUIRE(error.has_value());
    CHECK(error->code() == "http_request_timeout");
    CHECK(hw::is_connection_error(*error));
    CHECK(transport.requests.size() == 2);
}

TEST_CASE("Transport RequestError on post propagates without retry", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(hw::RequestError("timeout", "boom"));

    hw::Session session(transport);
    const auto error = thrown<hw::RequestError>(
        [&] { session.post("api/set", sample_body()); });

    REQUIRE(error.has_value());
    CHECK(error->code() == "http_request_timeout");
    CHECK(transport.requests.size() == 2);
}

TEST_CASE("Expect::Ok passes on OK, throws xml_response_not_ok otherwise and keeps rotation",
          "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"t"}));
    transport.script.push_back(FakeTransport::xml(std::string(kOk)));
    transport.script.push_back(FakeTransport::xml(
        std::string(kNotOk), hw::Headers{{"__RequestVerificationToken", "n"}}));
    transport.script.push_back(FakeTransport::xml(std::string(kNotOk)));

    hw::Session session(transport);
    CHECK_NOTHROW(session.post("api/ok", sample_body(), hw::Expect::Ok));

    const auto error = thrown<hw::RouterError>(
        [&] { session.post("api/bad", sample_body(), hw::Expect::Ok); });
    REQUIRE(error.has_value());
    CHECK(error->code() == "xml_response_not_ok");
    // The front token "t" was consumed by the first post; the error response appended "n".
    CHECK(tokens_of(session) == std::vector<std::string>{"n"});

    CHECK_NOTHROW(session.post("api/any", sample_body(), hw::Expect::Any));

    REQUIRE(transport.requests.size() == 4);
    CHECK(transport.requests[1].path == "api/ok");
    CHECK(transport.requests[2].path == "api/bad");
    CHECK(token_of(transport.requests[2]) == std::nullopt);
    CHECK(transport.requests[3].path == "api/any");
    CHECK(token_of(transport.requests[3]) == "n");
}

TEST_CASE("Explicit refresh_tokens replaces the previous tokens", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(FakeTransport::home_page({"x", "y"}));
    transport.script.push_back(ok("1"));

    hw::Session session(transport);
    session.refresh_tokens();
    session.refresh_tokens();
    CHECK(tokens_of(session) == std::vector<std::string>{"x", "y"});

    const auto response = session.get("api/x");

    CHECK(response.root().text("v") == "1");
    REQUIRE(transport.requests.size() == 3);
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(transport.requests[1].path == "html/home.html");
    CHECK(transport.requests[2].path == "api/x");
    CHECK(token_of(transport.requests[2]) == "x");
    CHECK(tokens_of(session) == std::vector<std::string>{"x", "y"});
}

TEST_CASE("Log emits one line per request without tokens or bodies", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"tokA"}));
    transport.script.push_back(ok("1", hw::Headers{{"__RequestVerificationTokenOne", "tokNew"}}));
    transport.script.push_back(ok("2"));

    std::vector<std::string> lines;
    hw::Session session(transport, [&](std::string_view line) { lines.emplace_back(line); });
    CHECK(session.post("api/set", hw::XmlRequest().add("Secret", "bodysecret123"))
              .root()
              .text("v") == "1");
    CHECK(session.get("api/x").root().text("v") == "2");

    CHECK(lines == std::vector<std::string>{"GET html/home.html -> 200",
                                            "POST api/set -> 200",
                                            "GET api/x -> 200"});
    for (const auto& line : lines) {
        CHECK(line.find("tokA") == std::string::npos);
        CHECK(line.find("tokNew") == std::string::npos);
        CHECK(line.find("bodysecret123") == std::string::npos);
    }
}

TEST_CASE("Log reports the HTTP status of each request", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"t"}));
    auto redirect = ok("1");
    redirect.status = 302;
    transport.script.push_back(redirect);

    std::vector<std::string> lines;
    hw::Session session(transport, [&](std::string_view line) { lines.emplace_back(line); });
    CHECK_NOTHROW(session.get("api/x"));

    CHECK(lines == std::vector<std::string>{"GET html/home.html -> 200", "GET api/x -> 302"});
}

TEST_CASE("Log includes the api/webserver/token fallback request", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({}));
    transport.script.push_back(FakeTransport::xml("<response><token>" + std::string(32, 'K') +
                                                  "</token></response>"));
    transport.script.push_back(ok("1"));

    std::vector<std::string> lines;
    hw::Session session(transport, [&](std::string_view line) { lines.emplace_back(line); });
    CHECK(session.get("api/x").root().text("v") == "1");

    CHECK(lines == std::vector<std::string>{"GET html/home.html -> 200",
                                            "GET api/webserver/token -> 200",
                                            "GET api/x -> 200"});
}

TEST_CASE("Log has one line per HTTP request, including token-error retries", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a"}));
    transport.script.push_back(FakeTransport::error(125001));
    transport.script.push_back(FakeTransport::home_page({"b"}));
    transport.script.push_back(ok("2"));

    std::vector<std::string> lines;
    hw::Session session(transport, [&](std::string_view line) { lines.emplace_back(line); });
    CHECK(session.get("api/x").root().text("v") == "2");

    CHECK(lines == std::vector<std::string>{"GET html/home.html -> 200",
                                            "GET api/x -> 200",
                                            "GET html/home.html -> 200",
                                            "GET api/x -> 200"});
    CHECK(lines.size() == transport.requests.size());
}

TEST_CASE("A 200 KB meta content value is extracted without crashing", "[session]") {
    const std::string big(200 * 1024, 'x');
    FakeTransport transport;
    transport.script.push_back(FakeTransport::xml(
        "<html><head><meta name=\"csrf_token\" content=\"" + big + "\"></head></html>"));
    transport.script.push_back(ok("1"));

    hw::Session session(transport);
    CHECK(session.get("api/x").root().text("v") == "1");

    REQUIRE(transport.requests.size() == 2);
    CHECK(token_of(transport.requests[1]) == big);
    CHECK(tokens_of(session) == std::vector<std::string>{big});
}

TEST_CASE("A 200 KB junk attribute before name and content is skipped", "[session]") {
    const std::string junk(200 * 1024, 'j');
    FakeTransport transport;
    transport.script.push_back(FakeTransport::xml(
        "<html><head><meta data-junk=\"" + junk +
        "\" name=\"csrf_token\" content=\"tok\"></head></html>"));
    transport.script.push_back(ok("1"));

    hw::Session session(transport);
    CHECK(session.get("api/x").root().text("v") == "1");

    REQUIRE(transport.requests.size() == 2);
    CHECK(token_of(transport.requests[1]) == "tok");
    CHECK(tokens_of(session) == std::vector<std::string>{"tok"});
}

TEST_CASE("Meta tag and attribute names are matched case-insensitively", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::xml(
        "<HTML><HEAD><META NAME=\"csrf_token\" CONTENT=\"u\"></HEAD></HTML>"));
    transport.script.push_back(ok("1"));

    hw::Session session(transport);
    CHECK(session.get("api/x").root().text("v") == "1");

    REQUIRE(transport.requests.size() == 2);
    CHECK(token_of(transport.requests[1]) == "u");
    CHECK(tokens_of(session) == std::vector<std::string>{"u"});
}

TEST_CASE("Unterminated meta tag yields no token and falls back to api/webserver/token",
          "[session]") {
    const std::string fallback(32, 'F');
    FakeTransport transport;
    transport.script.push_back(FakeTransport::xml(
        "<html><head><meta name=\"csrf_token\" content=\"z\""));
    transport.script.push_back(FakeTransport::xml("<response><token>" + fallback +
                                                  "</token></response>"));
    transport.script.push_back(ok("1"));

    hw::Session session(transport);
    CHECK(session.get("api/x").root().text("v") == "1");

    REQUIRE(transport.requests.size() == 3);
    CHECK(transport.requests[1].path == "api/webserver/token");
    CHECK(token_of(transport.requests[2]) == fallback);
    CHECK(tokens_of(session) == std::vector<std::string>{fallback});
}

TEST_CASE("Post with an invalid XML body still appends rotation tokens", "[session]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({"a", "b"}));
    transport.script.push_back(
        FakeTransport::xml("garbage", hw::Headers{{"__RequestVerificationToken", "n"}}));

    hw::Session session(transport);
    const auto error = thrown<hw::RequestError>(
        [&] { session.post("api/set", sample_body()); });

    REQUIRE(error.has_value());
    CHECK(error->code() == "http_request_invalid_xml");
    CHECK(transport.requests.size() == 2);
    CHECK(tokens_of(session) == std::vector<std::string>{"b", "n"});
}
