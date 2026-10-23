#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "hw/session.hpp"
#include "hw/xml.hpp"

namespace hw::api {

struct SendSmsRequest {
    std::vector<std::string> numbers;
    std::string content;
    int index = -1;          // -1 = new message (drafts use their index)
};

struct SendStatus {
    int total_count = 0;
    int cur_index = 0;
    std::string phone;
    std::string success_phones;
    std::string failed_phones;
    bool complete() const noexcept { return cur_index >= total_count; }
};

using Clock = std::function<std::chrono::system_clock::time_point()>;

// Send-SMS endpoints, mirroring src/sms.js.
class Sms {
public:
    explicit Sms(Session& session, Clock clock = [] { return std::chrono::system_clock::now(); });
    XmlRequest build_send_request(const SendSmsRequest& request) const;
    void send(const SendSmsRequest& request);        // POST api/sms/send-sms, Expect::Ok
    SendStatus send_status();                        // GET api/sms/send-status

private:
    Session& session_;
    Clock clock_;
};

}  // namespace hw::api
