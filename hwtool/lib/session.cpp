#include "hw/session.hpp"

#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "hw/errors.hpp"
#include "hw/transport.hpp"
#include "hw/xml.hpp"

namespace hw {

namespace {

constexpr std::string_view kTokenHeader = "__RequestVerificationToken";
constexpr std::string_view kTokenHeaderOne = "__RequestVerificationTokenOne";
constexpr std::string_view kTokenHeaderTwo = "__RequestVerificationTokenTwo";
constexpr std::string_view kContentTypeHeader = "Content-Type";
constexpr std::string_view kFormContentType = "application/x-www-form-urlencoded; charset=UTF-8";
constexpr std::string_view kLoginPath = "api/user/login";
constexpr std::string_view kHomePath = "html/home.html";
constexpr std::string_view kTokenPath = "api/webserver/token";
constexpr std::string_view kMetaTag = "<meta";
// Newer firmwares prefix the real token with this many junk characters.
constexpr std::size_t kJunkPrefixLength = 32;

char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool iequals_ascii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (lower_ascii(a[i]) != lower_ascii(b[i])) {
            return false;
        }
    }
    return true;
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

std::size_t skip_spaces(std::string_view text, std::size_t i) {
    while (i < text.size() && is_space(text[i])) {
        ++i;
    }
    return i;
}

// Position of the next "<meta" tag at or after from, or npos. The tag name must end at
// whitespace, "/" or ">", so "<metadata" does not match.
std::size_t find_meta_tag(std::string_view html, std::size_t from) {
    for (std::size_t p = html.find('<', from); p != std::string_view::npos;
         p = html.find('<', p + 1)) {
        if (p + kMetaTag.size() < html.size() &&
            iequals_ascii(html.substr(p, kMetaTag.size()), kMetaTag)) {
            const char next = html[p + kMetaTag.size()];
            if (is_space(next) || next == '/' || next == '>') {
                return p;
            }
        }
    }
    return std::string_view::npos;
}

// The content of a csrf_token meta tag, given the text between "<meta" and ">". Attributes are
// walked once: name, optional spaces, "=", then a quoted or unquoted value.
std::optional<std::string_view> csrf_content(std::string_view attributes) {
    std::optional<std::string_view> name;
    std::optional<std::string_view> content;
    std::size_t i = 0;
    while (i < attributes.size()) {
        if (is_space(attributes[i]) || attributes[i] == '/') {
            ++i;
            continue;
        }
        const std::size_t key_start = i;
        while (i < attributes.size() && attributes[i] != '=' && attributes[i] != '/' &&
               attributes[i] != '"' && attributes[i] != '\'' && !is_space(attributes[i])) {
            ++i;
        }
        if (i == key_start) {
            ++i;  // stray character such as '=' or a quote: skip it
            continue;
        }
        const std::string_view key = attributes.substr(key_start, i - key_start);
        i = skip_spaces(attributes, i);
        if (i >= attributes.size() || attributes[i] != '=') {
            continue;  // attribute without a value
        }
        i = skip_spaces(attributes, i + 1);
        if (i >= attributes.size()) {
            break;
        }

        std::string_view value;
        if (attributes[i] == '"' || attributes[i] == '\'') {
            const std::size_t close = attributes.find(attributes[i], i + 1);
            if (close == std::string_view::npos) {
                break;
            }
            value = attributes.substr(i + 1, close - i - 1);
            i = close + 1;
        } else {
            const std::size_t start = i;
            while (i < attributes.size() && !is_space(attributes[i])) {
                ++i;
            }
            value = attributes.substr(start, i - start);
        }

        if (iequals_ascii(key, "name")) {
            name = value;
        } else if (iequals_ascii(key, "content")) {
            content = value;
        }
    }
    if (name && *name == "csrf_token") {
        return content;
    }
    return std::nullopt;
}

// Values of <meta name="csrf_token" content="..."> tags, in document order.
std::deque<std::string> meta_tokens(std::string_view html) {
    std::deque<std::string> out;
    std::size_t tag = find_meta_tag(html, 0);
    while (tag != std::string_view::npos) {
        const std::size_t close = html.find('>', tag + kMetaTag.size());
        if (close == std::string_view::npos) {
            break;  // unterminated tag: nothing after it can be closed either
        }
        const auto content =
            csrf_content(html.substr(tag + kMetaTag.size(), close - tag - kMetaTag.size()));
        if (content) {
            out.emplace_back(*content);
        }
        tag = find_meta_tag(html, close + 1);
    }
    return out;
}

}  // namespace

Session::Session(ITransport& transport, LogSink log)
    : transport_(transport), log_(std::move(log)) {}

XmlResponse Session::get(std::string_view path) {
    ensure_tokens();
    try {
        return get_once(path);
    } catch (const RouterApiError& e) {
        if (!is_token_error(e)) {
            throw;
        }
    }
    refresh_tokens();
    return get_once(path);
}

XmlResponse Session::post(std::string_view path, const XmlRequest& body, Expect expect) {
    ensure_tokens();
    const std::string payload = body.str();
    try {
        return post_once(path, payload, expect);
    } catch (const RouterApiError& e) {
        if (!is_token_error(e)) {
            throw;
        }
    }
    refresh_tokens();
    return post_once(path, payload, expect);
}

void Session::refresh_tokens() {
    tokens_ = fetch_tokens();
    tokens_loaded_ = true;
}

const std::deque<std::string>& Session::tokens() const noexcept {
    return tokens_;
}

HttpResponse Session::send_logged(const HttpRequest& request) {
    HttpResponse response = transport_.send(request);
    if (log_) {
        log_(request.method + " " + request.path + " -> " + std::to_string(response.status));
    }
    return response;
}

XmlResponse Session::get_once(std::string_view path) {
    HttpRequest request;
    request.method = "GET";
    request.path = std::string(path);
    if (!tokens_.empty()) {
        request.headers[std::string(kTokenHeader)] = tokens_.front();
    }
    return XmlResponse::parse(send_logged(request).body);
}

XmlResponse Session::post_once(std::string_view path, const std::string& payload,
                               Expect expect) {
    HttpRequest request;
    request.method = "POST";
    request.path = std::string(path);
    request.body = payload;
    request.headers[std::string(kContentTypeHeader)] = std::string(kFormContentType);
    if (!tokens_.empty()) {
        request.headers[std::string(kTokenHeader)] = tokens_.front();
        tokens_.pop_front();
    }
    return finish_post(path, send_logged(request), expect);
}

// Parses a POST response and updates the token pool. Rotation tokens are kept on every parse
// failure, including malformed XML, so the pool matches what the router issued.
XmlResponse Session::finish_post(std::string_view path, const HttpResponse& http,
                                 Expect expect) {
    std::optional<XmlResponse> parsed;
    try {
        parsed = XmlResponse::parse(http.body);
    } catch (...) {
        append_rotated(http.headers);
        throw;
    }
    if (path == kLoginPath) {
        tokens_.clear();
    }
    append_rotated(http.headers);
    if (expect == Expect::Ok) {
        parsed->require_ok();
    }
    return std::move(*parsed);
}

// Fetches the pool from home.html, falling back to api/webserver/token. A router error from the
// fallback means the firmware has no CSRF tokens, which is not an error here.
std::deque<std::string> Session::fetch_tokens() {
    HttpRequest home;
    home.path = std::string(kHomePath);
    const HttpResponse page = send_logged(home);
    std::deque<std::string> from_page = meta_tokens(page.body);
    if (!from_page.empty()) {
        return from_page;
    }

    HttpRequest fallback;
    fallback.path = std::string(kTokenPath);
    try {
        std::string token = XmlResponse::parse(send_logged(fallback).body).root().text("token");
        if (token.size() > kJunkPrefixLength) {
            token = token.substr(kJunkPrefixLength);
        }
        return {token};
    } catch (const RouterApiError&) {
        return {};
    }
}

void Session::ensure_tokens() {
    if (!tokens_loaded_) {
        refresh_tokens();
    }
}

// One response token pair (One, Two) takes precedence over the single token header.
void Session::append_rotated(const Headers& headers) {
    const auto one = headers.find(kTokenHeaderOne);
    if (one != headers.end()) {
        tokens_.push_back(one->second);
        const auto two = headers.find(kTokenHeaderTwo);
        if (two != headers.end()) {
            tokens_.push_back(two->second);
        }
        return;
    }
    const auto single = headers.find(kTokenHeader);
    if (single != headers.end()) {
        tokens_.push_back(single->second);
    }
}

}  // namespace hw
