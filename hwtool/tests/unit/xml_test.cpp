#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "hw/errors.hpp"
#include "hw/xml.hpp"

namespace {

constexpr std::string_view kDecl = R"(<?xml version="1.0" encoding="UTF-8"?>)";

// Full request document: declaration followed by body, nothing in between.
std::string xml_doc(std::string_view body) {
    return std::string(kDecl) + std::string(body);
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

// Text of each node, in order.
std::vector<std::string> texts_of(const std::vector<hw::XmlNode>& nodes) {
    std::vector<std::string> out;
    for (const auto& node : nodes) {
        out.push_back(node.text());
    }
    return out;
}

constexpr std::string_view kMessages =
    "<response><Count>2</Count><Messages>"
    "<Message><Index>40001</Index><Content>hi</Content></Message>"
    "<Message><Index>40002</Index><Content>yo</Content></Message>"
    "</Messages></response>";

}  // namespace

TEST_CASE("XmlRequest renders the pinned example without whitespace", "[xml]") {
    const std::string str = hw::XmlRequest()
                                .add("Index", -1)
                                .begin("Phones")
                                .add("Phone", "+48 1")
                                .add("Phone", "2")
                                .end()
                                .add("Sca", "")
                                .add("Content", "a<b&c> \xC4\x85\xC4\x99\xF0\x9F\x98\x80")
                                .str();
    CHECK(str == xml_doc("<request><Index>-1</Index><Phones><Phone>+48 1</Phone>"
                         "<Phone>2</Phone></Phones><Sca></Sca>"
                         "<Content>a&lt;b&amp;c&gt; \xC4\x85\xC4\x99\xF0\x9F\x98\x80"
                         "</Content></request>"));
}

TEST_CASE("XmlRequest with no calls renders an empty request", "[xml]") {
    CHECK(hw::XmlRequest().str() == xml_doc("<request></request>"));
}

TEST_CASE("XmlRequest renders an empty value as open and close tags", "[xml]") {
    CHECK(hw::XmlRequest().add("Sca", "").str() == xml_doc("<request><Sca></Sca></request>"));
}

TEST_CASE("XmlRequest escapes &, < and > in values", "[xml]") {
    CHECK(hw::XmlRequest().add("V", "&<>").str() ==
          xml_doc("<request><V>&amp;&lt;&gt;</V></request>"));
}

TEST_CASE("XmlRequest escapes ampersands once, without double-escaping", "[xml]") {
    CHECK(hw::XmlRequest().add("V", "&amp;").str() ==
          xml_doc("<request><V>&amp;amp;</V></request>"));
}

TEST_CASE("XmlRequest passes non-ASCII UTF-8 through unchanged", "[xml]") {
    CHECK(hw::XmlRequest().add("V", "\xC4\x85\xF0\x9F\x98\x80").str() ==
          xml_doc("<request><V>\xC4\x85\xF0\x9F\x98\x80</V></request>"));
}

TEST_CASE("XmlRequest renders long long values including zero", "[xml]") {
    CHECK(hw::XmlRequest().add("N", 0LL).str() == xml_doc("<request><N>0</N></request>"));
    CHECK(hw::XmlRequest().add("N", 1234567890123LL).str() ==
          xml_doc("<request><N>1234567890123</N></request>"));
}

TEST_CASE("XmlRequest nests begin/end elements", "[xml]") {
    CHECK(hw::XmlRequest()
              .begin("A")
              .begin("B")
              .add("X", "1")
              .end()
              .add("Y", "2")
              .end()
              .str() == xml_doc("<request><A><B><X>1</X></B><Y>2</Y></A></request>"));
}

TEST_CASE("XmlRequest::end without an open begin throws logic_error", "[xml]") {
    CHECK_THROWS_AS(hw::XmlRequest().end(), std::logic_error);
    CHECK_THROWS_AS(hw::XmlRequest().begin("A").end().end(), std::logic_error);
}

TEST_CASE("XmlRequest::str with an unclosed begin throws logic_error", "[xml]") {
    CHECK_THROWS_AS(hw::XmlRequest().begin("A").str(), std::logic_error);
    CHECK_THROWS_AS(hw::XmlRequest().begin("A").begin("B").end().str(), std::logic_error);
}

TEST_CASE("XmlRequest is move-only", "[xml]") {
    STATIC_REQUIRE(!std::is_copy_constructible_v<hw::XmlRequest>);
    STATIC_REQUIRE(!std::is_copy_assignable_v<hw::XmlRequest>);
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<hw::XmlRequest>);
}

TEST_CASE("XmlRequest keeps building correctly after a move", "[xml]") {
    hw::XmlRequest a;
    a.add("X", "1");
    hw::XmlRequest b = std::move(a);
    b.add("Y", "2");
    CHECK(b.str() == xml_doc("<request><X>1</X><Y>2</Y></request>"));
}

TEST_CASE("XmlResponse parses a pretty-printed Huawei response", "[xml]") {
    const auto response = hw::XmlResponse::parse(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<response>\n<State>-1</State>\n"
        "<Username></Username>\n<password_type>4</password_type>\n</response>\n");
    CHECK(response.root().name() == "response");
    CHECK(response.root().text("State") == "-1");
    CHECK(response.root().find("Username") == std::optional<std::string>(""));
    CHECK(response.root().find("Nope") == std::nullopt);

    const auto err = thrown<hw::RouterError>([&] { response.root().text("Nope"); });
    REQUIRE(err.has_value());
    CHECK(err->code() == "xml_missing_field");
    CHECK(std::string(err->what()) == "Nope");
}

TEST_CASE("XmlNode::find handles multi-segment and self-closing paths", "[xml]") {
    const auto response = hw::XmlResponse::parse(
        "<response><Messages><Message>1</Message></Messages><A><B></B></A>"
        "<Username/></response>");
    CHECK(response.root().find("Messages/Nope") == std::nullopt);
    CHECK(response.root().find("Nope/Index") == std::nullopt);
    CHECK(response.root().find("A/B") == std::optional<std::string>(""));
    CHECK(response.root().find("Username") == std::optional<std::string>(""));

    const auto err = thrown<hw::RouterError>([&] { response.root().text("Nope/Index"); });
    REQUIRE(err.has_value());
    CHECK(err->code() == "xml_missing_field");
}

TEST_CASE("XmlNode::children returns repeated elements in document order", "[xml]") {
    const auto response = hw::XmlResponse::parse(kMessages);
    const auto messages = response.root().children("Messages/Message");
    REQUIRE(messages.size() == 2);
    CHECK(messages[0].text("Index") == "40001");
    CHECK(messages[1].text("Content") == "yo");
}

TEST_CASE("XmlNode::children matches only direct children at the given level", "[xml]") {
    const auto response = hw::XmlResponse::parse(
        "<response><Message>top</Message><Messages><Message>a</Message><X/>"
        "<Message>b</Message></Messages><Other><Message>c</Message></Other></response>");
    const std::vector<std::string> ab{"a", "b"};
    const std::vector<std::string> c{"c"};
    const std::vector<std::string> top{"top"};
    CHECK(texts_of(response.root().children("Messages/Message")) == ab);
    CHECK(texts_of(response.root().children("Other/Message")) == c);
    CHECK(texts_of(response.root().children("Message")) == top);

    const auto deep = hw::XmlResponse::parse(
        "<response><Other><Messages><Message>x</Message></Messages></Other></response>");
    CHECK(deep.root().children("Messages/Message").empty());
}

TEST_CASE("XmlNode::text with a path returns the first matching element", "[xml]") {
    const auto response = hw::XmlResponse::parse(kMessages);
    CHECK(response.root().text("Messages/Message/Index") == "40001");
}

TEST_CASE("XmlNode::children is empty for a missing parent or leaf", "[xml]") {
    const auto response = hw::XmlResponse::parse(kMessages);
    CHECK(response.root().children("Missing/Message").empty());
    CHECK(response.root().children("Messages/Nope").empty());
}

TEST_CASE("XmlNode::children with a single segment returns direct children", "[xml]") {
    const auto response = hw::XmlResponse::parse(kMessages);
    const auto counts = response.root().children("Count");
    REQUIRE(counts.size() == 1);
    CHECK(counts[0].text() == "2");
}

TEST_CASE("XmlResponse decodes entities in text", "[xml]") {
    const auto response =
        hw::XmlResponse::parse("<response><Content>a&amp;b &lt;3</Content></response>");
    CHECK(response.root().text("Content") == "a&b <3");
}

TEST_CASE("XmlResponse decodes numeric character references", "[xml]") {
    const auto response = hw::XmlResponse::parse("<response><V>&#x41;&#66;</V></response>");
    CHECK(response.root().text("V") == "AB");
}

TEST_CASE("XmlResponse round-trips UTF-8 text unchanged", "[xml]") {
    const auto response = hw::XmlResponse::parse(
        "<response><V>\xC4\x85\xC4\x99\xF0\x9F\x98\x80</V></response>");
    CHECK(response.root().text("V") == "\xC4\x85\xC4\x99\xF0\x9F\x98\x80");
}

TEST_CASE("XmlNode stays valid after its temporary XmlResponse is gone", "[xml]") {
    REQUIRE(hw::XmlResponse::parse(kMessages).root().children("Messages/Message").size() == 2);
    auto node = hw::XmlResponse::parse(kMessages).root().children("Messages/Message").at(1);
    CHECK(node.name() == "Message");
    CHECK(node.text("Content") == "yo");
}

TEST_CASE("XmlNode copied from a temporary response stays valid", "[xml]") {
    hw::XmlNode root = hw::XmlResponse::parse(kMessages).root();
    CHECK(root.name() == "response");
    CHECK(root.text("Count") == "2");
    CHECK(root.children("Messages/Message").size() == 2);
    CHECK(root.text("Messages/Message/Index") == "40001");
}

TEST_CASE("XmlResponse::is_ok accepts OK in any case with surrounding whitespace", "[xml]") {
    CHECK(hw::XmlResponse::parse("<response>OK</response>").is_ok());
    CHECK(hw::XmlResponse::parse("<response>ok</response>").is_ok());
    CHECK(hw::XmlResponse::parse(
              "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<response>\nOK\n</response>")
              .is_ok());
}

TEST_CASE("XmlResponse::is_ok rejects other responses", "[xml]") {
    CHECK_FALSE(hw::XmlResponse::parse("<response><State>0</State></response>").is_ok());
    CHECK_FALSE(hw::XmlResponse::parse("<response>NOK</response>").is_ok());
}

TEST_CASE("XmlResponse::require_ok throws only when not ok", "[xml]") {
    CHECK_NOTHROW(hw::XmlResponse::parse("<response>OK</response>").require_ok());

    const auto err = thrown<hw::RouterError>(
        [] { hw::XmlResponse::parse("<response>NOK</response>").require_ok(); });
    REQUIRE(err.has_value());
    CHECK(err->code() == "xml_response_not_ok");
}

TEST_CASE("XmlResponse::parse throws RouterApiError for an error body", "[xml]") {
    const auto err = thrown<hw::RouterApiError>([] {
        hw::XmlResponse::parse(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<error>\n<code>108002</code>\n"
            "<message></message>\n</error>");
    });
    REQUIRE(err.has_value());
    CHECK(err->api_code() == 108002);
    CHECK(std::string(err->what()) == "login_password_wrong");
}

TEST_CASE("XmlResponse::parse accepts whitespace around the error code digits", "[xml]") {
    const auto err = thrown<hw::RouterApiError>([] {
        hw::XmlResponse::parse("<error><code> 108002 </code><message></message></error>");
    });
    REQUIRE(err.has_value());
    CHECK(err->api_code() == 108002);
}

TEST_CASE("XmlResponse::parse appends the router message to an error", "[xml]") {
    const auto err = thrown<hw::RouterApiError>([] {
        hw::XmlResponse::parse("<error><code>108002</code><message>Bad</message></error>");
    });
    REQUIRE(err.has_value());
    CHECK(std::string(err->what()) == "login_password_wrong : Bad");
}

TEST_CASE("XmlResponse::parse decodes entities in the error message", "[xml]") {
    const auto err = thrown<hw::RouterApiError>([] {
        hw::XmlResponse::parse("<error><code>108002</code><message>a&amp;b</message></error>");
    });
    REQUIRE(err.has_value());
    CHECK(std::string(err->what()) == "login_password_wrong : a&b");
}

TEST_CASE("XmlResponse::parse maps token errors for is_token_error", "[xml]") {
    const auto err = thrown<hw::RouterApiError>([] {
        hw::XmlResponse::parse("<error><code>125002</code><message></message></error>");
    });
    REQUIRE(err.has_value());
    CHECK(hw::is_token_error(*err));
}

TEST_CASE("XmlResponse::parse rejects an error body with a bad or missing code", "[xml]") {
    const std::string_view bodies[] = {
        "<error><code>abc</code></error>",
        "<error><code>12x</code></error>",
        "<error><code></code></error>",
        "<error><code>99999999999</code></error>",
        "<error></error>",
        "<error><message>M</message></error>",
    };
    for (const auto body : bodies) {
        INFO("body: " << body);
        const auto err = thrown<hw::RequestError>([&] { hw::XmlResponse::parse(body); });
        REQUIRE(err.has_value());
        CHECK(err->code() == "http_request_invalid_xml");
    }
}

TEST_CASE("XmlResponse::parse rejects malformed input as invalid_xml", "[xml]") {
    const std::string_view bodies[] = {
        "",
        "   ",
        "<response>",
        "<response></wrong>",
        "not xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>",
        "<!-- c -->",
        "<a/><b/>",
    };
    for (const auto body : bodies) {
        INFO("body: " << body);
        const auto err = thrown<hw::RequestError>([&] { hw::XmlResponse::parse(body); });
        REQUIRE(err.has_value());
        CHECK(err->code() == "http_request_invalid_xml");
    }
}
