// Inert stubs: the real implementation comes after the tests in this step are red.
#include "hw/xml.hpp"

#include <utility>

namespace hw {

XmlRequest::XmlRequest() {}

XmlRequest& XmlRequest::add(std::string_view, std::string_view) {
    return *this;
}

XmlRequest& XmlRequest::add(std::string_view, long long) {
    return *this;
}

XmlRequest& XmlRequest::begin(std::string_view) {
    return *this;
}

XmlRequest& XmlRequest::end() {
    return *this;
}

std::string XmlRequest::str() const {
    return {};
}

XmlNode::XmlNode() {}

XmlNode::XmlNode(std::shared_ptr<pugi::xml_document> doc, pugi::xml_node node)
    : doc_(std::move(doc)), node_(node) {}

std::string XmlNode::name() const {
    return {};
}

std::string XmlNode::text() const {
    return {};
}

std::optional<std::string> XmlNode::find(std::string_view) const {
    return std::nullopt;
}

std::string XmlNode::text(std::string_view) const {
    return {};
}

std::vector<XmlNode> XmlNode::children(std::string_view) const {
    return {};
}

XmlResponse::XmlResponse() {}

XmlResponse XmlResponse::parse(std::string_view) {
    return XmlResponse();
}

const XmlNode& XmlResponse::root() const {
    return root_;
}

bool XmlResponse::is_ok() const {
    return false;
}

void XmlResponse::require_ok() const {}

}  // namespace hw
