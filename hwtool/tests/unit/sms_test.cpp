#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "hw/api/sms.hpp"
#include "hw/errors.hpp"
#include "hw/session.hpp"
#include "hw/transport.hpp"
#include "support/fake_transport.hpp"

namespace {

// Sets TZ for the lifetime of the guard and restores the previous value (or unsets it).
class TzGuard {
public:
    explicit TzGuard(const char* zone) {
        if (const char* old = std::getenv("TZ")) {
            previous_ = std::string(old);
        }
        setenv("TZ", zone, 1);
        tzset();
    }
    ~TzGuard() {
        if (previous_) {
            setenv("TZ", previous_->c_str(), 1);
        } else {
            unsetenv("TZ");
        }
        tzset();
    }
    TzGuard(const TzGuard&) = delete;
    TzGuard& operator=(const TzGuard&) = delete;

private:
    std::optional<std::string> previous_;
};

constexpr std::string_view kTokenHeader = "__RequestVerificationToken";
constexpr std::string_view kDecl = R"(<?xml version="1.0" encoding="UTF-8"?>)";
constexpr std::string_view kHomeToken = "TOKEN0123456789abcdef0123456789ab";
constexpr std::string_view kDate = "2026-03-05 07:08:09";
constexpr std::string_view kEmoji = "\xF0\x9F\x98\x80";

// Value of the named request header, or nullopt if it is absent.
std::optional<std::string> token_of(const hw::HttpRequest& request) {
    const auto it = request.headers.find(kTokenHeader);
    if (it == request.headers.end()) {
        return std::nullopt;
    }
    return it->second;
}

// Local time 2026-03-05 07:08:09, built with mktime so the test is independent of TZ.
std::chrono::system_clock::time_point fixed_time() {
    std::tm tm{};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 3 - 1;
    tm.tm_mday = 5;
    tm.tm_hour = 7;
    tm.tm_min = 8;
    tm.tm_sec = 9;
    tm.tm_isdst = -1;
    return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

hw::api::Clock fixed_clock() {
    return [] { return fixed_time(); };
}

// <Phone>...</Phone> elements for each number, in order.
std::string phones_xml(const std::vector<std::string>& numbers) {
    std::string out;
    for (const auto& number : numbers) {
        out += "<Phone>" + number + "</Phone>";
    }
    return out;
}

// Full request body with the fixed date. phones is the inner XML of <Phones>.
std::string sms_body(std::string_view index, std::string_view phones,
                     std::string_view content_escaped, std::size_t length) {
    return std::string(kDecl) + "<request><Index>" + std::string(index) + "</Index><Phones>" +
           std::string(phones) + "</Phones><Sca></Sca><Content>" +
           std::string(content_escaped) + "</Content><Length>" + std::to_string(length) +
           "</Length><Reserved>1</Reserved><Date>" + std::string(kDate) +
           "</Date></request>";
}

hw::api::SendSmsRequest request_of(std::vector<std::string> numbers, std::string content,
                                   int index = -1) {
    hw::api::SendSmsRequest request;
    request.numbers = std::move(numbers);
    request.content = std::move(content);
    request.index = index;
    return request;
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

TEST_CASE("build_send_request renders the exact body with local date and UTF-16 length",
          "[sms]") {
    FakeTransport transport;
    hw::Session session(transport);
    const std::string expected = sms_body("-1", phones_xml({"+48100200"}), "Hi", 2);

    SECTION("process time zone") {
        hw::api::Sms sms(session, fixed_clock());
        CHECK(sms.build_send_request(request_of({"+48100200"}, "Hi")).str() == expected);
    }
    SECTION("zone with a half-hour offset") {
        TzGuard zone("Asia/Kolkata");
        hw::api::Sms sms(session, fixed_clock());
        CHECK(sms.build_send_request(request_of({"+48100200"}, "Hi")).str() == expected);
    }
    CHECK(transport.requests.empty());
}

TEST_CASE("build_send_request keeps the draft index and phone order", "[sms]") {
    FakeTransport transport;
    hw::Session session(transport);
    hw::api::Sms sms(session, fixed_clock());

    const auto body =
        sms.build_send_request(request_of({"+481", "+482", "+483"}, "Hello", 40003));

    CHECK(body.str() == sms_body("40003", phones_xml({"+481", "+482", "+483"}), "Hello", 5));
}

TEST_CASE("build_send_request counts Length in UTF-16 units", "[sms]") {
    FakeTransport transport;
    hw::Session session(transport);
    hw::api::Sms sms(session, fixed_clock());

    SECTION("emoji is a surrogate pair") {
        const auto body =
            sms.build_send_request(request_of({"+481"}, std::string("Hi ") + std::string(kEmoji)));
        CHECK(body.str() ==
              sms_body("-1", phones_xml({"+481"}), std::string("Hi ") + std::string(kEmoji), 5));
    }
    SECTION("two-byte characters are one unit each") {
        const auto body = sms.build_send_request(request_of({"+481"}, "\xC4\x85\xC4\x99"));
        CHECK(body.str() == sms_body("-1", phones_xml({"+481"}), "\xC4\x85\xC4\x99", 2));
    }
}

TEST_CASE("build_send_request escapes content and counts the raw characters", "[sms]") {
    FakeTransport transport;
    hw::Session session(transport);
    hw::api::Sms sms(session, fixed_clock());

    const auto body = sms.build_send_request(request_of({"+481"}, "a<b"));

    CHECK(body.str() == sms_body("-1", phones_xml({"+481"}), "a&lt;b", 3));
}

TEST_CASE("build_send_request rejects invalid input with std::invalid_argument", "[sms]") {
    FakeTransport transport;
    hw::Session session(transport);
    hw::api::Sms sms(session, fixed_clock());

    SECTION("no numbers") {
        CHECK(thrown<std::invalid_argument>([&] { sms.build_send_request(request_of({}, "Hi")); })
                  .has_value());
    }
    SECTION("empty number") {
        CHECK(thrown<std::invalid_argument>([&] {
                  sms.build_send_request(request_of({"+481", ""}, "Hi"));
              }).has_value());
    }
    SECTION("empty content") {
        CHECK(thrown<std::invalid_argument>([&] {
                  sms.build_send_request(request_of({"+481"}, ""));
              }).has_value());
    }
    SECTION("malformed UTF-8 content") {
        for (const auto& bad : {std::string("\xC3"), std::string("\x80"),
                                      std::string("\xC0\x80")}) {
            CHECK(thrown<std::invalid_argument>([&] {
                      sms.build_send_request(request_of({"+481"}, bad));
                  }).has_value());
        }
    }
    CHECK(transport.requests.empty());
}

TEST_CASE("send posts the exact body with Expect::Ok and succeeds on OK", "[sms]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(FakeTransport::xml("<response>OK</response>"));

    hw::Session session(transport);
    hw::api::Sms sms(session, fixed_clock());
    sms.send(request_of({"+481", "+482"}, "Hi"));

    REQUIRE(transport.requests.size() == 2);
    CHECK(transport.requests[0].method == "GET");
    CHECK(transport.requests[0].path == "html/home.html");
    CHECK(transport.requests[1].method == "POST");
    CHECK(transport.requests[1].path == "api/sms/send-sms");
    CHECK(token_of(transport.requests[1]) == std::string(kHomeToken));
    CHECK(transport.requests[1].body == sms_body("-1", phones_xml({"+481", "+482"}), "Hi", 2));
}

TEST_CASE("send throws xml_response_not_ok when the router answers FAIL", "[sms]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(FakeTransport::xml("<response>FAIL</response>"));

    hw::Session session(transport);
    hw::api::Sms sms(session, fixed_clock());
    const auto error = thrown<hw::RouterError>([&] { sms.send(request_of({"+481"}, "Hi")); });

    REQUIRE(error.has_value());
    CHECK(error->code() == "xml_response_not_ok");
    REQUIRE(transport.requests.size() == 2);
    CHECK(transport.requests[1].path == "api/sms/send-sms");
}

TEST_CASE("send propagates a router error such as 113004 as RouterApiError", "[sms]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(FakeTransport::error(113004));

    hw::Session session(transport);
    hw::api::Sms sms(session, fixed_clock());
    const auto error = thrown<hw::RouterApiError>([&] { sms.send(request_of({"+481"}, "Hi")); });

    REQUIRE(error.has_value());
    CHECK(error->api_code() == 113004);
    REQUIRE(transport.requests.size() == 2);
}

TEST_CASE("send with invalid arguments throws before any HTTP request", "[sms]") {
    FakeTransport transport;
    hw::Session session(transport);
    hw::api::Sms sms(session, fixed_clock());

    SECTION("no numbers") {
        CHECK(thrown<std::invalid_argument>([&] { sms.send(request_of({}, "Hi")); }).has_value());
    }
    SECTION("empty number") {
        CHECK(thrown<std::invalid_argument>([&] { sms.send(request_of({"+481", ""}, "Hi")); })
                  .has_value());
    }
    SECTION("empty content") {
        CHECK(thrown<std::invalid_argument>([&] { sms.send(request_of({"+481"}, "")); })
                  .has_value());
    }
    SECTION("malformed UTF-8 content") {
        CHECK(thrown<std::invalid_argument>([&] { sms.send(request_of({"+481"}, "\xC3")); })
                  .has_value());
    }
    CHECK(transport.requests.empty());
}

TEST_CASE("send_status maps every field from GET api/sms/send-status", "[sms]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(FakeTransport::xml(
        "<response><TotalCount>2</TotalCount><CurIndex>1</CurIndex><Phone>+481</Phone>"
        "<SucPhone>+480</SucPhone><FailPhone></FailPhone></response>"));

    hw::Session session(transport);
    hw::api::Sms sms(session, fixed_clock());
    const auto status = sms.send_status();

    CHECK(status.total_count == 2);
    CHECK(status.cur_index == 1);
    CHECK(status.phone == "+481");
    CHECK(status.success_phones == "+480");
    CHECK(status.failed_phones.empty());

    REQUIRE(transport.requests.size() == 2);
    CHECK(transport.requests[1].method == "GET");
    CHECK(transport.requests[1].path == "api/sms/send-status");
    CHECK(token_of(transport.requests[1]) == std::string(kHomeToken));
    CHECK(transport.requests[1].body.empty());
}

TEST_CASE("send_status maps empty or missing numbers to 0 and strings to empty", "[sms]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(FakeTransport::xml(
        "<response><TotalCount></TotalCount><Phone>+481</Phone></response>"));

    hw::Session session(transport);
    hw::api::Sms sms(session, fixed_clock());
    const auto status = sms.send_status();

    CHECK(status.total_count == 0);
    CHECK(status.cur_index == 0);
    CHECK(status.phone == "+481");
    CHECK(status.success_phones.empty());
    CHECK(status.failed_phones.empty());
    REQUIRE(transport.requests.size() == 2);
}

TEST_CASE("send_status rejects TotalCount or CurIndex that is not a strict integer", "[sms]") {
    const auto value = GENERATE(as<std::string>{}, "x", "2x", "99999999999");
    const auto field = GENERATE(as<std::string>{}, "TotalCount", "CurIndex");
    const std::string total = field == "TotalCount" ? value : std::string("2");
    const std::string cur = field == "CurIndex" ? value : std::string("1");

    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(FakeTransport::xml("<response><TotalCount>" + total +
                                                  "</TotalCount><CurIndex>" + cur +
                                                  "</CurIndex></response>"));

    hw::Session session(transport);
    hw::api::Sms sms(session, fixed_clock());
    const auto error = thrown<hw::RouterError>([&] { sms.send_status(); });

    REQUIRE(error.has_value());
    CHECK(error->code() == "invalid_response");
    REQUIRE(transport.requests.size() == 2);
}

TEST_CASE("send_status reads an empty CurIndex as 0", "[sms]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(FakeTransport::xml(
        "<response><TotalCount>2</TotalCount><CurIndex></CurIndex></response>"));

    hw::Session session(transport);
    hw::api::Sms sms(session, fixed_clock());
    const auto status = sms.send_status();

    CHECK(status.total_count == 2);
    CHECK(status.cur_index == 0);
    REQUIRE(transport.requests.size() == 2);
}

TEST_CASE("SendStatus is complete once cur_index reaches or exceeds total_count", "[sms]") {
    FakeTransport transport;
    transport.script.push_back(FakeTransport::home_page({std::string(kHomeToken)}));
    transport.script.push_back(FakeTransport::xml(
        "<response><TotalCount>2</TotalCount><CurIndex>1</CurIndex></response>"));
    transport.script.push_back(FakeTransport::xml(
        "<response><TotalCount>2</TotalCount><CurIndex>2</CurIndex></response>"));
    transport.script.push_back(FakeTransport::xml(
        "<response><TotalCount>2</TotalCount><CurIndex>3</CurIndex></response>"));
    transport.script.push_back(FakeTransport::xml("<response></response>"));

    hw::Session session(transport);
    hw::api::Sms sms(session, fixed_clock());

    const auto in_progress = sms.send_status();
    CHECK(in_progress.total_count == 2);
    CHECK(in_progress.cur_index == 1);
    CHECK_FALSE(in_progress.complete());

    const auto done = sms.send_status();
    CHECK(done.cur_index == 2);
    CHECK(done.complete());

    const auto over = sms.send_status();
    CHECK(over.total_count == 2);
    CHECK(over.cur_index == 3);
    CHECK(over.complete());

    const auto empty = sms.send_status();
    CHECK(empty.total_count == 0);
    CHECK(empty.cur_index == 0);
    CHECK(empty.complete());
    REQUIRE(transport.requests.size() == 5);
}
