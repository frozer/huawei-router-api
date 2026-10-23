#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string_view>

#include "hw/util/utf.hpp"

TEST_CASE("utf16_length counts UTF-16 code units", "[utf]") {
    CHECK(hw::util::utf16_length("") == 0);
    CHECK(hw::util::utf16_length("hello") == 5);
    CHECK(hw::util::utf16_length("\xC4\x85\xC4\x99") == 2);  // "ąę"
    // "привет"
    CHECK(hw::util::utf16_length("\xD0\xBF\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82") == 6);
    CHECK(hw::util::utf16_length("\xE2\x82\xAC") == 1);  // "€"
    CHECK(hw::util::utf16_length("\xF0\x9F\x98\x80") == 2);  // "😀"
    CHECK(hw::util::utf16_length("a\xF0\x9F\x98\x80" "b") == 4);  // "a😀b"
}

TEST_CASE("utf16_length handles valid code point boundaries", "[utf]") {
    CHECK(hw::util::utf16_length("\x7F") == 1);                  // U+007F
    CHECK(hw::util::utf16_length("\xC2\x80") == 1);              // U+0080
    CHECK(hw::util::utf16_length("\xDF\xBF") == 1);              // U+07FF
    CHECK(hw::util::utf16_length("\xE0\xA0\x80") == 1);          // U+0800
    CHECK(hw::util::utf16_length("\xED\x9F\xBF") == 1);          // U+D7FF
    CHECK(hw::util::utf16_length("\xEE\x80\x80") == 1);          // U+E000
    CHECK(hw::util::utf16_length("\xEF\xBF\xBF") == 1);          // U+FFFF
    CHECK(hw::util::utf16_length("\xF0\x90\x80\x80") == 2);      // U+10000
    CHECK(hw::util::utf16_length("\xF4\x8F\xBF\xBF") == 2);      // U+10FFFF
    CHECK(hw::util::utf16_length(std::string_view("a\0b", 3)) == 3);  // embedded NUL
}

TEST_CASE("utf16_length rejects malformed UTF-8", "[utf]") {
    CHECK_THROWS_AS(hw::util::utf16_length("\xC3"), std::invalid_argument);  // truncated
    CHECK_THROWS_AS(hw::util::utf16_length("\xE2\x82"), std::invalid_argument);  // truncated
    CHECK_THROWS_AS(hw::util::utf16_length("\xF0\x9F\x98"), std::invalid_argument);  // truncated
    CHECK_THROWS_AS(hw::util::utf16_length("\x80"), std::invalid_argument);  // stray continuation
    CHECK_THROWS_AS(hw::util::utf16_length("\xFF"), std::invalid_argument);  // invalid lead
    CHECK_THROWS_AS(hw::util::utf16_length("\xF8\x88\x80\x80\x80"),
                    std::invalid_argument);  // invalid lead
    CHECK_THROWS_AS(hw::util::utf16_length("\xF5\x80\x80\x80"),
                    std::invalid_argument);  // invalid lead
    CHECK_THROWS_AS(hw::util::utf16_length("\xC0\xAF"), std::invalid_argument);  // overlong 2-byte
    CHECK_THROWS_AS(hw::util::utf16_length("\xE0\x80\x80"),
                    std::invalid_argument);  // overlong 3-byte
    CHECK_THROWS_AS(hw::util::utf16_length("\xF0\x80\x80\x80"),
                    std::invalid_argument);  // overlong 4-byte
    CHECK_THROWS_AS(hw::util::utf16_length("\xED\xA0\x80"),
                    std::invalid_argument);  // surrogate
    CHECK_THROWS_AS(hw::util::utf16_length("\xED\xBF\xBF"),
                    std::invalid_argument);  // surrogate end
    CHECK_THROWS_AS(hw::util::utf16_length("\xF4\x90\x80\x80"),
                    std::invalid_argument);  // above U+10FFFF
    CHECK_THROWS_AS(hw::util::utf16_length("\xE2\x28\xA1"),
                    std::invalid_argument);  // bad continuation
    CHECK_THROWS_AS(hw::util::utf16_length("a\xC3"), std::invalid_argument);  // after valid prefix
    CHECK_THROWS_AS(hw::util::utf16_length("a\xC3(b"), std::invalid_argument);  // mid-string
}
