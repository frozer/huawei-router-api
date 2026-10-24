#include "send_sms.hpp"

#include <CLI/CLI.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "context.hpp"
#include "exit_codes.hpp"
#include "hw/api/admin.hpp"
#include "hw/api/sms.hpp"
#include "hw/errors.hpp"
#include "hw/session.hpp"
#include "output.hpp"
#include "validators.hpp"

namespace hwtool {

namespace {

constexpr std::chrono::milliseconds kPollInterval{500};
constexpr std::string_view kWhitespace = " \t\r\n";

std::string_view trim(std::string_view s) {
    const auto first = s.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(kWhitespace);
    return s.substr(first, last - first + 1);
}

// Splits a delivery-status list on ';' and ',', trims entries and drops empty ones.
std::vector<std::string> split_status_list(std::string_view text) {
    std::vector<std::string> items;
    std::size_t start = 0;
    while (true) {
        std::size_t end = text.find_first_of(";,", start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view item = trim(text.substr(start, end - start));
        if (!item.empty()) {
            items.emplace_back(item);
        }
        if (end == text.size()) {
            break;
        }
        start = end + 1;
    }
    return items;
}

// Splits a --to entry on ',' and trims each piece. Empty pieces are kept, so that "+1,,+2"
// reaches build_send_request and is rejected there.
void append_numbers(std::vector<std::string>& numbers, std::string_view entry) {
    std::size_t start = 0;
    while (true) {
        std::size_t end = entry.find(',', start);
        if (end == std::string_view::npos) {
            end = entry.size();
        }
        numbers.emplace_back(trim(entry.substr(start, end - start)));
        if (end == entry.size()) {
            break;
        }
        start = end + 1;
    }
}

std::string join(const std::vector<std::string>& items) {
    std::string out;
    for (const auto& item : items) {
        if (!out.empty()) {
            out += ", ";
        }
        out += item;
    }
    return out;
}

}  // namespace

SendSmsCommand::SendSmsCommand(Sleeper sleep) : sleep_(std::move(sleep)) {}

void SendSmsCommand::configure(CLI::App& sub) {
    sub.add_option("-t,--to", to_, "Recipient number(s), repeatable or comma-separated")
        ->required();
    sub.add_option("-m,--message", message_, "Message text")->required();
    sub.add_flag("--wait", wait_, "Wait for the router to report delivery status");
    sub.add_option("--wait-timeout", wait_timeout_,
                   "Seconds to wait for delivery status with --wait (> 0)")
        ->check(finite_positive())
        ->capture_default_str();
}

int SendSmsCommand::run(Context& ctx) {
    const hw::Credentials credentials = ctx.credentials();

    hw::api::SendSmsRequest request;
    for (const auto& entry : to_) {
        append_numbers(request.numbers, entry);
    }
    request.content = message_;

    // Validation only: no request is sent by build_send_request.
    hw::Session& session = ctx.session();
    hw::api::Sms sms(session);
    sms.build_send_request(request);

    hw::api::Admin(session).ensure_logged_in(credentials);
    sms.send(request);

    Output& output = ctx.output();
    const std::string numbers_text = join(request.numbers);
    if (!wait_) {
        output.result("SMS sent to " + numbers_text,
                      {Field{"status", std::string("sent")}, Field{"numbers", request.numbers}});
        return static_cast<int>(ExitCode::ok);
    }

    const auto started = std::chrono::steady_clock::now();
    const std::chrono::duration<double> limit(wait_timeout_);
    hw::api::SendStatus status;
    while (true) {
        status = sms.send_status();
        if (status.complete()) {
            break;
        }
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;
        if (elapsed >= limit) {
            throw hw::RouterError("sms_send_timeout",
                                  "timed out waiting for SMS delivery status");
        }
        sleep_(kPollInterval);
    }

    const std::vector<std::string> succeeded = split_status_list(status.success_phones);
    const std::vector<std::string> failed = split_status_list(status.failed_phones);
    if (!failed.empty()) {
        const std::string state = succeeded.empty() ? "failed" : "partial";
        output.result("SMS failed for " + join(failed),
                      {Field{"status", state}, Field{"numbers", request.numbers},
                       Field{"succeeded", succeeded}, Field{"failed", failed}});
        return static_cast<int>(ExitCode::partial);
    }
    output.result("SMS delivered to " + numbers_text,
                  {Field{"status", std::string("delivered")}, Field{"numbers", request.numbers},
                   Field{"succeeded", succeeded}, Field{"failed", failed}});
    return static_cast<int>(ExitCode::ok);
}

Sleeper SendSmsCommand::default_sleeper() {
    return [](std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); };
}

}  // namespace hwtool
