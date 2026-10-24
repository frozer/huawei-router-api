#include "hw/api/sms.hpp"

#include <cstddef>
#include <ctime>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "hw/session.hpp"
#include "hw/transport.hpp"
#include "hw/util/utf.hpp"
#include "hw/xml.hpp"
#include "../util/parse.hpp"

namespace hw::api {

namespace {

constexpr std::string_view kSendPath = "api/sms/send-sms";
constexpr std::string_view kStatusPath = "api/sms/send-status";
constexpr std::size_t kDateBufferSize = 32;
constexpr long long kReserved = 1;  // text mode field, see src/sms.js

// Local time as "YYYY-MM-DD HH:MM:SS", zero-padded.
std::string format_local(std::chrono::system_clock::time_point when) {
    const std::time_t time = std::chrono::system_clock::to_time_t(when);
    std::tm local{};
    if (localtime_r(&time, &local) == nullptr) {
        throw std::runtime_error("localtime_r failed");
    }
    char buffer[kDateBufferSize];
    const std::size_t written = std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
    return std::string(buffer, written);
}

// Element text as an int; missing or blank elements read as 0.
int optional_int(const XmlNode& node, std::string_view field) {
    const auto value = node.find(field);
    if (!value || util::trim(*value).empty()) {
        return 0;
    }
    return util::parse_int(field, *value);
}

std::string optional_text(const XmlNode& node, std::string_view field) {
    return node.find(field).value_or(std::string());
}

}  // namespace

Sms::Sms(Session& session, Clock clock) : session_(session), clock_(std::move(clock)) {}

XmlRequest Sms::build_send_request(const SendSmsRequest& request) const {
    if (request.numbers.empty()) {
        throw std::invalid_argument("no phone numbers");
    }
    for (const auto& number : request.numbers) {
        if (number.empty()) {
            throw std::invalid_argument("empty phone number");
        }
    }
    if (request.content.empty()) {
        throw std::invalid_argument("empty content");
    }
    // Throws std::invalid_argument on malformed UTF-8.
    const std::size_t length = util::utf16_length(request.content);

    XmlRequest body;
    body.add("Index", static_cast<long long>(request.index)).begin("Phones");
    for (const auto& number : request.numbers) {
        body.add("Phone", number);
    }
    body.end()
        .add("Sca", "")
        .add("Content", request.content)
        .add("Length", static_cast<long long>(length))
        .add("Reserved", kReserved)
        .add("Date", format_local(clock_()));
    return body;
}

void Sms::send(const SendSmsRequest& request) {
    session_.post(kSendPath, build_send_request(request), Expect::Ok);
}

SendStatus Sms::send_status() {
    const XmlResponse response = session_.get(kStatusPath);
    const XmlNode& root = response.root();

    SendStatus status;
    status.total_count = optional_int(root, "TotalCount");
    status.cur_index = optional_int(root, "CurIndex");
    status.phone = optional_text(root, "Phone");
    status.success_phones = optional_text(root, "SucPhone");
    status.failed_phones = optional_text(root, "FailPhone");
    return status;
}

}  // namespace hw::api
