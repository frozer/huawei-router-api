#pragma once

#include <pugixml.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hw {

// Builds a request body such as <?xml ...?><request><Index>1</Index></request>.
// Values are escaped (&, <, >); no whitespace is emitted.
class XmlRequest {
public:
    XmlRequest();  // root element <request>
    // Move-only: a copy would share the underlying document with the original.
    XmlRequest(const XmlRequest&) = delete;
    XmlRequest& operator=(const XmlRequest&) = delete;
    XmlRequest(XmlRequest&&) noexcept = default;
    XmlRequest& operator=(XmlRequest&&) noexcept = default;
    ~XmlRequest() = default;

    XmlRequest& add(std::string_view name, std::string_view value);
    XmlRequest& add(std::string_view name, long long value);
    XmlRequest& begin(std::string_view name);  // open nested element
    XmlRequest& end();  // close it; std::logic_error if none open
    std::string str() const;  // std::logic_error if a begin() is unclosed

private:
    std::shared_ptr<pugi::xml_document> doc_;
    pugi::xml_node root_;
    pugi::xml_node current_;
};

// Read-only view of an element. Keeps its document alive, so it outlives the parent response.
class XmlNode {
public:
    std::string name() const;
    std::string text() const;  // own text content
    std::optional<std::string> find(std::string_view path) const;  // "A/B" relative to this node
    std::string text(std::string_view path) const;  // throws RouterError "xml_missing_field"
    std::vector<XmlNode> children(std::string_view path) const;  // empty if parent missing

private:
    friend class XmlResponse;

    XmlNode();
    XmlNode(std::shared_ptr<pugi::xml_document> doc, pugi::xml_node node);

    std::shared_ptr<pugi::xml_document> doc_;
    pugi::xml_node node_;
};

// Parsed router response. Error responses (<error><code>...) are reported by throwing.
class XmlResponse {
public:
    // Throws RouterApiError for <error> bodies, RequestError "http_request_invalid_xml"
    // for malformed input.
    static XmlResponse parse(std::string_view body);

    const XmlNode& root() const;
    bool is_ok() const;  // root text, trimmed, case-insensitive == "ok"
    void require_ok() const;  // throws RouterError "xml_response_not_ok" otherwise

private:
    XmlResponse();

    XmlNode root_;
};

}  // namespace hw
