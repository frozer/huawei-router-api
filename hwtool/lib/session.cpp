#include "hw/session.hpp"

#include <deque>
#include <string>
#include <string_view>
#include <utility>

namespace hw {

// Stub: intentionally inert so that the tests fail until the real session is implemented.
Session::Session(ITransport& transport, LogSink log)
    : transport_(transport), log_(std::move(log)) {}

XmlResponse Session::get(std::string_view) {
    return XmlResponse::parse("<response></response>");
}

XmlResponse Session::post(std::string_view, const XmlRequest&, Expect) {
    return XmlResponse::parse("<response></response>");
}

void Session::refresh_tokens() {}

const std::deque<std::string>& Session::tokens() const noexcept {
    return tokens_;
}

}  // namespace hw
