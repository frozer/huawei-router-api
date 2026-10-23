#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "output.hpp"

using hwtool::Field;
using hwtool::json_escape;
using hwtool::Output;

TEST_CASE("json_escape escapes quotes, backslashes and short control characters", "[output]") {
    CHECK(json_escape("say \"hi\"") == R"(say \"hi\")");
    CHECK(json_escape("a\\b") == R"(a\\b)");
    CHECK(json_escape("\n") == R"(\n)");
    CHECK(json_escape("\r") == R"(\r)");
    CHECK(json_escape("\t") == R"(\t)");
    CHECK(json_escape("\b") == R"(\b)");
    CHECK(json_escape("\f") == R"(\f)");
}

TEST_CASE("json_escape writes other control characters as lowercase \\u00XX", "[output]") {
    CHECK(json_escape("\x01") == R"(\u0001)");
    CHECK(json_escape("\x1f") == R"(\u001f)");
}

TEST_CASE("json_escape handles DEL, NUL, vertical tab and combined input", "[output]") {
    CHECK(json_escape("\x7f") == "\x7f");
    CHECK(json_escape(std::string_view("\0", 1)) == R"(\u0000)");
    CHECK(json_escape("\x0b") == R"(\u000b)");
    CHECK(json_escape("a\"\\\n") == R"(a\"\\\n)");
}

TEST_CASE("json_escape leaves plain text and UTF-8 bytes unchanged", "[output]") {
    CHECK(json_escape("plain text 123") == "plain text 123");
    CHECK(json_escape("ąę😀") == "ąę😀");
}

TEST_CASE("Text result writes the summary line and nothing to err", "[output]") {
    std::ostringstream out;
    std::ostringstream err;
    Output output(out, err, false);

    output.result("SMS sent", {Field{"status", std::string("sent")}});

    CHECK(!output.json());
    CHECK(out.str() == "SMS sent\n");
    CHECK(err.str().empty());
}

TEST_CASE("JSON result writes fields of all value types in order", "[output]") {
    std::ostringstream out;
    std::ostringstream err;
    Output output(out, err, true);

    output.result("SMS sent",
                  {Field{"status", std::string("sent")}, Field{"count", 2LL},
                   Field{"ok", true},
                   Field{"numbers", std::vector<std::string>{"+1", "+2"}}});

    CHECK(out.str() == R"({"status":"sent","count":2,"ok":true,"numbers":["+1","+2"]})"
                       "\n");
    CHECK(err.str().empty());
}

TEST_CASE("JSON result escapes string values", "[output]") {
    std::ostringstream out;
    std::ostringstream err;
    Output output(out, err, true);

    output.result("x", {Field{"msg", std::string("a\"b\n")}});

    CHECK(out.str() == R"({"msg":"a\"b\n"})"
                       "\n");
}

TEST_CASE("JSON result escapes vector elements", "[output]") {
    std::ostringstream out;
    std::ostringstream err;
    Output output(out, err, true);

    output.result("x", {Field{"a", std::vector<std::string>{"a\"b", "c\\d"}}});

    CHECK(out.str() == R"({"a":["a\"b","c\\d"]})"
                       "\n");
}

TEST_CASE("JSON result renders negative, zero and false values", "[output]") {
    std::ostringstream out;
    std::ostringstream err;
    Output output(out, err, true);

    output.result("x", {Field{"n", -5LL}, Field{"z", 0LL}, Field{"f", false}});

    CHECK(out.str() == R"({"n":-5,"z":0,"f":false})"
                       "\n");
}

TEST_CASE("JSON error escapes code and message", "[output]") {
    std::ostringstream out;
    std::ostringstream err;
    Output output(out, err, true);

    output.error("c\"x", "line1\nsay \"hi\"\\");

    CHECK(out.str().empty());
    CHECK(err.str() == R"({"error":{"code":"c\"x","message":"line1\nsay \"hi\"\\"}})"
                       "\n");
}

TEST_CASE("JSON result renders an empty vector as []", "[output]") {
    std::ostringstream out;
    std::ostringstream err;
    Output output(out, err, true);

    output.result("x", {Field{"numbers", std::vector<std::string>{}}});

    CHECK(out.str() == R"({"numbers":[]})"
                       "\n");
}

TEST_CASE("JSON result with no fields writes an empty object", "[output]") {
    std::ostringstream out;
    std::ostringstream err;
    Output output(out, err, true);

    output.result("done", {});

    CHECK(out.str() == "{}\n");
    CHECK(err.str().empty());
}

TEST_CASE("Text error writes the bracketed code line to err", "[output]") {
    std::ostringstream out;
    std::ostringstream err;
    Output output(out, err, false);

    output.error("bad", "boom");

    CHECK(out.str().empty());
    CHECK(err.str() == "hwtool: error: boom [bad]\n");
}

TEST_CASE("JSON error writes the error object line to err", "[output]") {
    std::ostringstream out;
    std::ostringstream err;
    Output output(out, err, true);

    output.error("bad", "boom");

    CHECK(out.str().empty());
    CHECK(err.str() == R"({"error":{"code":"bad","message":"boom"}})"
                       "\n");
}

TEST_CASE("Log writes the prefixed line to err in text mode", "[output]") {
    std::ostringstream out;
    std::ostringstream err;
    Output output(out, err, false);

    output.log("GET x -> 200");

    CHECK(out.str().empty());
    CHECK(err.str() == "hwtool: GET x -> 200\n");
}

TEST_CASE("Log writes the prefixed line to err in JSON mode", "[output]") {
    std::ostringstream out;
    std::ostringstream err;
    Output output(out, err, true);

    output.log("GET x -> 200");

    CHECK(out.str().empty());
    CHECK(err.str() == "hwtool: GET x -> 200\n");
}
