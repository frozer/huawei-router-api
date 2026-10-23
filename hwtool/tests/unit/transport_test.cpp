#include <catch2/catch_test_macros.hpp>

#include <string>

#include "hw/transport.hpp"

TEST_CASE("Header lookup ignores ASCII case", "[transport]") {
    hw::Headers headers;
    headers.insert({"Content-Type", "text/plain"});

    auto lower = headers.find("content-type");
    REQUIRE(lower != headers.end());
    CHECK(lower->second == "text/plain");

    auto upper = headers.find("CONTENT-TYPE");
    REQUIRE(upper != headers.end());
    CHECK(upper->second == "text/plain");
}

TEST_CASE("Distinct header names are kept apart", "[transport]") {
    hw::Headers headers;
    headers["Content-Type"] = "text/plain";
    headers["Content-Length"] = "42";

    CHECK(headers.size() == 2);
    auto length = headers.find("content-length");
    REQUIRE(length != headers.end());
    CHECK(length->second == "42");
}

TEST_CASE("Header names that are prefixes of each other stay apart", "[transport]") {
    hw::Headers headers;
    headers["Accept"] = "text/html";
    headers["Accept-Encoding"] = "gzip";

    CHECK(headers.size() == 2);
    REQUIRE(headers.find("accept") != headers.end());
    CHECK(headers.find("accept")->second == "text/html");
    REQUIRE(headers.find("ACCEPT-ENCODING") != headers.end());
    CHECK(headers.find("ACCEPT-ENCODING")->second == "gzip");
}

TEST_CASE("Non-ASCII header names are distinct keys and do not crash", "[transport]") {
    hw::Headers headers;
    headers["\xC3\xA9"] = "lower";
    headers["\xC3\x89"] = "upper";

    CHECK(headers.size() == 2);
    REQUIRE(headers.find("\xC3\xA9") != headers.end());
    CHECK(headers.find("\xC3\xA9")->second == "lower");
    REQUIRE(headers.find("\xC3\x89") != headers.end());
    CHECK(headers.find("\xC3\x89")->second == "upper");
}

TEST_CASE("operator[] with differing case overwrites the same entry", "[transport]") {
    hw::Headers headers;
    headers["Accept"] = "a";
    headers["ACCEPT"] = "b";

    CHECK(headers.size() == 1);
    REQUIRE(headers.find("accept") != headers.end());
    CHECK(headers.find("accept")->second == "b");
}

TEST_CASE("CaseInsensitiveLess orders names ignoring ASCII case", "[transport]") {
    const hw::CaseInsensitiveLess less;

    CHECK(less("abc", "abd"));
    CHECK(less("a", "B"));
    CHECK_FALSE(less("B", "a"));
    CHECK_FALSE(less("abc", "ABC"));
    CHECK_FALSE(less("ABC", "abc"));
    CHECK_FALSE(less("abd", "abc"));
}

TEST_CASE("CaseInsensitiveLess handles prefixes and empty names", "[transport]") {
    const hw::CaseInsensitiveLess less;

    CHECK(less("a", "AB"));
    CHECK_FALSE(less("AB", "a"));
    CHECK_FALSE(less("", ""));
    CHECK(less("", "a"));
}
