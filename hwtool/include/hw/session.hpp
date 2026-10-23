#pragma once

#include <deque>
#include <functional>
#include <string>
#include <string_view>

#include "hw/transport.hpp"
#include "hw/xml.hpp"

namespace hw {

// Receives one line per HTTP request, e.g. "GET html/home.html -> 200". Never contains tokens
// or request bodies.
using LogSink = std::function<void(std::string_view line)>;

enum class Expect { Any, Ok };

// Authenticated XML API session: CSRF token pool, rotation and one retry on token errors.
class Session {
public:
    explicit Session(ITransport& transport, LogSink log = {});

    XmlResponse get(std::string_view path);
    XmlResponse post(std::string_view path, const XmlRequest& body, Expect expect = Expect::Any);
    void refresh_tokens();
    const std::deque<std::string>& tokens() const noexcept;

private:
    HttpResponse send_logged(const HttpRequest& request);
    XmlResponse get_once(std::string_view path);
    XmlResponse post_once(std::string_view path, const std::string& payload, Expect expect);
    XmlResponse finish_post(std::string_view path, const HttpResponse& http, Expect expect);
    std::deque<std::string> fetch_tokens();
    void ensure_tokens();
    void append_rotated(const Headers& headers);

    ITransport& transport_;
    LogSink log_;
    std::deque<std::string> tokens_;
    bool tokens_loaded_ = false;
};

}  // namespace hw
