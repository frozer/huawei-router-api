#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "command.hpp"

namespace hwtool {

using Sleeper = std::function<void(std::chrono::milliseconds)>;

// "send-sms": sends one SMS to one or more numbers, optionally waiting for delivery status.
class SendSmsCommand : public Command {
public:
    explicit SendSmsCommand(Sleeper sleep = default_sleeper());
    void configure(CLI::App& sub) override;
    int run(Context& ctx) override;
    static Sleeper default_sleeper();

private:
    Sleeper sleep_;
    std::vector<std::string> to_;
    std::string message_;
    bool wait_ = false;
    double wait_timeout_ = 30.0;  // seconds
};

}  // namespace hwtool
