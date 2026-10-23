#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>

#include "cli.hpp"
#include "commands/send_sms.hpp"
#include "context.hpp"
#include "hw/curl_transport.hpp"
#include "hw/transport.hpp"
#include "registry.hpp"

int main(int argc, char** argv) {
    hwtool::CommandRegistry registry;
    registry.add<hwtool::SendSmsCommand>("send-sms", "Send an SMS message");

    hwtool::TransportFactory factory =
        [](const hwtool::GlobalOptions& options) -> std::unique_ptr<hw::ITransport> {
        using Millis = std::chrono::milliseconds;
        // Cap at one year before converting, so huge --timeout values cannot overflow llround.
        const double capped = std::min(options.timeout * 1000.0, 86400000.0 * 365);
        const auto millis = static_cast<Millis::rep>(std::llround(capped));
        return std::make_unique<hw::CurlTransport>(options.url, Millis(millis));
    };

    return hwtool::run_cli(argc, argv, registry, std::move(factory), std::cout, std::cerr);
}
