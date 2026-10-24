#include "hw/xml.hpp"

#include <cctype>
#include <charconv>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "hw/errors.hpp"

namespace hw {

namespace {

constexpr std::string_view kDeclaration = R"(<?xml version="1.0" encoding="UTF-8"?>)";
constexpr std::string_view kAsciiWhitespace = " \t\r\n\f\v";

// A moved-from XmlRequest has no document; using it would touch the moved-to document.
void require_not_moved(const std::shared_ptr<pugi::xml_document>& doc) {
    if (!doc) {
        throw std::logic_error("XmlRequest used after move");
    }
}

// Values and names are passed to pugixml as C strings, so an embedded NUL would truncate them.
void require_no_nul(std::string_view text, const char* message) {
    if (text.find('\0') != std::string_view::npos) {
        throw std::logic_error(message);
    }
}

// Collects pugixml's output into a std::string.
class StringWriter : public pugi::xml_writer {
public:
    void write(const void* data, std::size_t size) override {
        out_.append(static_cast<const char*>(data), size);
    }

    std::string take() {
        return std::move(out_);
    }

private:
    std::string out_;
};

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(kAsciiWhitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(kAsciiWhitespace);
    return std::string(text.substr(first, last - first + 1));
}

bool iequals_ascii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = std::tolower(static_cast<unsigned char>(a[i]));
        const auto cb = std::tolower(static_cast<unsigned char>(b[i]));
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

// Concatenated character data directly under node (not in descendant elements).
std::string own_text(pugi::xml_node node) {
    std::string out;
    for (pugi::xml_node child = node.first_child(); child; child = child.next_sibling()) {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
            out += child.value();
        }
    }
    return out;
}

// Walks "A/B/C" through exact direct-child names; first match per segment.
// Returns an empty node if any segment is missing.
pugi::xml_node resolve_path(pugi::xml_node node, std::string_view path) {
    while (true) {
        const auto slash = path.find('/');
        const std::string segment(path.substr(0, slash));
        node = node.child(segment.c_str());
        if (!node || slash == std::string_view::npos) {
            return node;
        }
        path.remove_prefix(slash + 1);
    }
}

}  // namespace

XmlRequest::XmlRequest()
    : doc_(std::make_shared<pugi::xml_document>()),
      root_(doc_->append_child("request")),
      current_(root_) {}

XmlRequest& XmlRequest::add(std::string_view name, std::string_view value) {
    require_not_moved(doc_);
    require_no_nul(name, "XmlRequest element name contains a NUL byte");
    require_no_nul(value, "XmlRequest value contains a NUL byte");
    pugi::xml_node element = current_.append_child(std::string(name).c_str());
    if (!value.empty()) {
        element.append_child(pugi::node_pcdata).set_value(std::string(value).c_str());
    }
    return *this;
}

XmlRequest& XmlRequest::add(std::string_view name, long long value) {
    require_not_moved(doc_);
    return add(name, std::to_string(value));
}

XmlRequest& XmlRequest::begin(std::string_view name) {
    require_not_moved(doc_);
    require_no_nul(name, "XmlRequest element name contains a NUL byte");
    current_ = current_.append_child(std::string(name).c_str());
    return *this;
}

XmlRequest& XmlRequest::end() {
    require_not_moved(doc_);
    if (current_ == root_) {
        throw std::logic_error("XmlRequest::end without a matching begin()");
    }
    current_ = current_.parent();
    return *this;
}

std::string XmlRequest::str() const {
    require_not_moved(doc_);
    if (current_ != root_) {
        throw std::logic_error("XmlRequest::str with an unclosed begin()");
    }
    StringWriter writer;
    doc_->save(writer, "",
               pugi::format_raw | pugi::format_no_declaration | pugi::format_no_empty_element_tags);
    return std::string(kDeclaration) + writer.take();
}

std::string XmlNode::name() const {
    return node_.name();
}

std::string XmlNode::text() const {
    return own_text(node_);
}

std::optional<std::string> XmlNode::find(std::string_view path) const {
    const pugi::xml_node target = resolve_path(node_, path);
    if (!target) {
        return std::nullopt;
    }
    return own_text(target);
}

std::string XmlNode::text(std::string_view path) const {
    const auto value = find(path);
    if (!value) {
        throw RouterError("xml_missing_field", std::string(path));
    }
    return *value;
}

std::vector<XmlNode> XmlNode::children(std::string_view path) const {
    std::vector<XmlNode> result;
    const auto slash = path.rfind('/');
    const std::string_view leaf = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const pugi::xml_node parent =
        slash == std::string_view::npos ? node_ : resolve_path(node_, path.substr(0, slash));
    if (!parent) {
        return result;
    }
    const std::string name(leaf);
    for (pugi::xml_node child = parent.child(name.c_str()); child;
         child = child.next_sibling(name.c_str())) {
        result.push_back(XmlNode(doc_, child));
    }
    return result;
}

XmlNode::XmlNode() {}

XmlNode::XmlNode(std::shared_ptr<pugi::xml_document> doc, pugi::xml_node node)
    : doc_(std::move(doc)), node_(node) {}

XmlResponse::XmlResponse() {}

XmlResponse XmlResponse::parse(std::string_view body) {
    if (body.find('\0') != std::string_view::npos) {
        throw RequestError("invalid_xml", "NUL byte in body");
    }
    auto doc = std::make_shared<pugi::xml_document>();
    const pugi::xml_parse_result parsed = doc->load_buffer(body.data(), body.size());
    if (!parsed) {
        throw RequestError("invalid_xml", parsed.description());
    }

    pugi::xml_node root;
    std::size_t elements = 0;
    for (pugi::xml_node child = doc->first_child(); child; child = child.next_sibling()) {
        if (child.type() == pugi::node_element) {
            root = child;
            ++elements;
        }
    }
    if (elements != 1) {
        throw RequestError("invalid_xml", elements == 0 ? "no root element"
                                                        : "multiple root elements");
    }

    if (std::string_view(root.name()) == "error") {
        const pugi::xml_node code_node = root.child("code");
        if (!code_node) {
            throw RequestError("invalid_xml", "error response without code");
        }
        const std::string code_text = trim(own_text(code_node));
        int code = 0;
        const char* first = code_text.data();
        const char* last = first + code_text.size();
        const auto [ptr, ec] = std::from_chars(first, last, code);
        if (ec != std::errc() || ptr != last) {
            throw RequestError("invalid_xml", "error code is not an integer");
        }
        throw RouterApiError(code, own_text(root.child("message")));
    }

    XmlResponse response;
    response.root_ = XmlNode(doc, root);
    return response;
}

const XmlNode& XmlResponse::root() const {
    return root_;
}

bool XmlResponse::is_ok() const {
    return iequals_ascii(trim(root_.text()), "ok");
}

void XmlResponse::require_ok() const {
    if (!is_ok()) {
        throw RouterError("xml_response_not_ok", trim(root_.text()));
    }
}

}  // namespace hw
