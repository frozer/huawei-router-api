#include "hw/api/sms.hpp"

namespace hw::api {

// Inert stub: implemented in a later step.
Sms::Sms(Session& session, Clock clock) : session_(session), clock_(std::move(clock)) {}

XmlRequest Sms::build_send_request(const SendSmsRequest&) const {
    return XmlRequest();
}

void Sms::send(const SendSmsRequest&) {}

SendStatus Sms::send_status() {
    return SendStatus{};
}

}  // namespace hw::api
