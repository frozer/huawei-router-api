#include "send_sms.hpp"

#include <chrono>
#include <thread>
#include <utility>

#include "context.hpp"

namespace hwtool {

SendSmsCommand::SendSmsCommand(Sleeper sleep) : sleep_(std::move(sleep)) {}

void SendSmsCommand::configure(CLI::App&) {}

int SendSmsCommand::run(Context&) {
    return 1;
}

Sleeper SendSmsCommand::default_sleeper() {
    return [](std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); };
}

}  // namespace hwtool
