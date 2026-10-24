#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "cli.hpp"
#include "commands/send_sms.hpp"
#include "context.hpp"
#include "hw/errors.hpp"
#include "hw/transport.hpp"
#include "output.hpp"
#include "registry.hpp"
#include "support/fake_transport.hpp"

using Catch::Matchers::ContainsSubstring;
using std::chrono::milliseconds;

namespace {

constexpr std::string_view kTokenHeader = "__RequestVerificationToken";
const std::string kLoggedOut =
    "<response><State>-1</State><Username></Username><password_type>4</password_type></response>";
const std::string kLoggedIn =
    "<response><State>0</State><Username></Username><password_type>4</password_type></response>";
const std::string kOk = "<response>OK</response>";

// Sleeps requested by the command. Reset before every test.
std::vector<milliseconds> sleeps;

// Records the requested duration and really sleeps 30 ms, so timeouts depend on real time.
void record_sleep(milliseconds duration) {
    sleeps.push_back(duration);
    std::this_thread::sleep_for(milliseconds(30));
}

struct FastSendSms : hwtool::SendSmsCommand {
    FastSendSms() : SendSmsCommand(record_sleep) {}
};

// Forwards every request to a FakeTransport owned by the test, so the test can inspect it.
class ForwardTransport : public hw::ITransport {
public:
    explicit ForwardTransport(FakeTransport& inner) : inner_(inner) {}

    hw::HttpResponse send(const hw::HttpRequest& request) override { return inner_.send(request); }

private:
    FakeTransport& inner_;
};

struct Harness {
    FakeTransport transport;

    hwtool::TransportFactory factory() {
        return [this](const hwtool::GlobalOptions&) -> std::unique_ptr<hw::ITransport> {
            return std::make_unique<ForwardTransport>(transport);
        };
    }
};

struct RunResult {
    int code;
    std::string out;
    std::string err;
};

// Runs hwtool with "send-sms" registered as FastSendSms. "hwtool" is prepended to args.
RunResult run_send(std::vector<const char*> args, Harness& harness) {
    hwtool::CommandRegistry registry;
    registry.add<FastSendSms>("send-sms", "Send an SMS");
    std::ostringstream out;
    std::ostringstream err;
    args.insert(args.begin(), "hwtool");
    const int code = hwtool::run_cli(static_cast<int>(args.size()), args.data(), registry,
                                     harness.factory(), out, err);
    return {code, out.str(), err.str()};
}

// Sets an environment variable (or unsets it when value is null) and restores the previous
// state on destruction.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name) {
        if (const char* previous = std::getenv(name)) {
            previous_ = std::string(previous);
        }
        if (value != nullptr) {
            setenv(name_, value, 1);
        } else {
            unsetenv(name_);
        }
    }
    ~ScopedEnv() {
        if (previous_) {
            setenv(name_, previous_->c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    const char* name_;
    std::optional<std::string> previous_;
};

struct SmsFixture {
    // Keeps HWTOOL_PASSWORD unset unless a test sets it.
    ScopedEnv env{"HWTOOL_PASSWORD", nullptr};
    Harness harness;

    SmsFixture() { sleeps.clear(); }
};

hw::HttpResponse login_ok() {
    return FakeTransport::xml(kOk, hw::Headers{{"__RequestVerificationTokenOne", "t3"}});
}

// Scripts home page, login state and a successful login (rotation token t3).
void script_login(FakeTransport& transport, const std::string& state = kLoggedOut) {
    transport.script.push_back(FakeTransport::home_page({"t1", "t2"}));
    transport.script.push_back(FakeTransport::xml(state));
    if (state != kLoggedIn) {
        transport.script.push_back(login_ok());
    }
}

// Status poll response with the given counters and delivery lists.
hw::HttpResponse status(int total, int cur, const std::string& succeeded = "",
                        const std::string& failed = "") {
    return FakeTransport::xml("<response><TotalCount>" + std::to_string(total) +
                              "</TotalCount><CurIndex>" + std::to_string(cur) +
                              "</CurIndex><Phone></Phone><SucPhone>" + succeeded +
                              "</SucPhone><FailPhone>" + failed + "</FailPhone></response>");
}

// Value of the __RequestVerificationToken header, or "" when absent.
std::string token_value(const hw::HttpRequest& request) {
    const auto it = request.headers.find(kTokenHeader);
    return it == request.headers.end() ? std::string() : it->second;
}

}  // namespace

TEST_CASE_METHOD(SmsFixture, "A plain send logs in, sends with the rotated token and reports",
                 "[send_sms]") {
    script_login(harness.transport);
    harness.transport.script.push_back(FakeTransport::xml(kOk));

    const RunResult r = run_send({"send-sms", "-t", "+48123456789", "-m", "Hello", "-p", "secret"},
                                 harness);

    CHECK(r.code == 0);
    CHECK(r.out == "SMS sent to +48123456789\n");
    CHECK(r.err.empty());
    REQUIRE(harness.transport.requests.size() == 4);
    const auto& requests = harness.transport.requests;
    CHECK(requests[0].path == "html/home.html");
    CHECK(requests[1].path == "api/user/state-login");
    CHECK(requests[2].path == "api/user/login");
    CHECK(requests[3].path == "api/sms/send-sms");
    CHECK_THAT(requests[2].body, ContainsSubstring("<Username>admin</Username>"));
    CHECK_THAT(requests[2].body, ContainsSubstring("<password_type>4</password_type>"));
    CHECK(token_value(requests[3]) == "t3");
    CHECK_THAT(requests[3].body, ContainsSubstring("<Phone>+48123456789</Phone>"));
    CHECK_THAT(requests[3].body, ContainsSubstring("<Content>Hello</Content>"));
    CHECK(sleeps.empty());
}

TEST_CASE_METHOD(SmsFixture, "Password comes from -p before the subcommand or HWTOOL_PASSWORD",
                 "[send_sms]") {
    SECTION("-p before the subcommand") {
        script_login(harness.transport);
        harness.transport.script.push_back(FakeTransport::xml(kOk));

        const RunResult r =
            run_send({"-p", "secret", "send-sms", "-t", "+48123456789", "-m", "Hello"}, harness);

        CHECK(r.code == 0);
        CHECK(harness.transport.requests.size() == 4);
    }
    SECTION("HWTOOL_PASSWORD") {
        ScopedEnv password("HWTOOL_PASSWORD", "secret");
        script_login(harness.transport);
        harness.transport.script.push_back(FakeTransport::xml(kOk));

        const RunResult r = run_send({"send-sms", "-t", "+48123456789", "-m", "Hello"}, harness);

        CHECK(r.code == 0);
        CHECK(harness.transport.requests.size() == 4);
    }
}

TEST_CASE_METHOD(SmsFixture, "An already logged in router is not logged in again", "[send_sms]") {
    script_login(harness.transport, kLoggedIn);
    harness.transport.script.push_back(FakeTransport::xml(kOk));

    const RunResult r = run_send({"send-sms", "-t", "+48123456789", "-m", "Hello", "-p", "secret"},
                                 harness);

    CHECK(r.code == 0);
    CHECK(r.out == "SMS sent to +48123456789\n");
    REQUIRE(harness.transport.requests.size() == 3);
    CHECK(harness.transport.requests[2].path == "api/sms/send-sms");
    for (const auto& request : harness.transport.requests) {
        CHECK(request.path != "api/user/login");
    }
}

TEST_CASE_METHOD(SmsFixture, "Several numbers are sent in order, trimmed and joined in text",
                 "[send_sms]") {
    std::vector<const char*> args;
    SECTION("repeated -t") {
        args = {"send-sms", "-t", "+1", "-t", "+2", "-m", "Hello", "-p", "secret"};
    }
    SECTION("comma-separated") {
        args = {"send-sms", "-t", "+1,+2", "-m", "Hello", "-p", "secret"};
    }
    SECTION("whitespace around entries") {
        args = {"send-sms", "-t", " +1 , +2 ", "-m", "Hello", "-p", "secret"};
    }
    script_login(harness.transport);
    harness.transport.script.push_back(FakeTransport::xml(kOk));

    const RunResult r = run_send(args, harness);

    CHECK(r.code == 0);
    CHECK(r.out == "SMS sent to +1, +2\n");
    REQUIRE(harness.transport.requests.size() == 4);
    CHECK_THAT(harness.transport.requests[3].body,
               ContainsSubstring("<Phones><Phone>+1</Phone><Phone>+2</Phone></Phones>"));
}

TEST_CASE_METHOD(SmsFixture, "Usage errors exit 2 with no requests and a message on err",
                 "[send_sms]") {
    std::vector<const char*> args;
    SECTION("missing -t") {
        args = {"send-sms", "-m", "Hello", "-p", "secret"};
    }
    SECTION("missing -m") {
        args = {"send-sms", "-t", "+1", "-p", "secret"};
    }
    SECTION("missing password") {
        args = {"send-sms", "-t", "+1", "-m", "Hello"};
    }
    SECTION("empty number entry") {
        args = {"send-sms", "-t", "+1,,+2", "-m", "Hello", "-p", "secret"};
    }
    SECTION("empty message") {
        args = {"send-sms", "-t", "+1", "-m", "", "-p", "secret"};
    }
    SECTION("invalid UTF-8 message") {
        args = {"send-sms", "-t", "+1", "-m", "\xC3", "-p", "secret"};
    }
    SECTION("--wait-timeout 0") {
        args = {"send-sms", "-t", "+1", "-m", "Hello", "-p", "secret", "--wait", "--wait-timeout",
                "0"};
    }
    SECTION("--wait-timeout -1") {
        args = {"send-sms", "-t", "+1", "-m", "Hello", "-p", "secret", "--wait",
                "--wait-timeout=-1"};
    }
    SECTION("--wait-timeout inf") {
        args = {"send-sms", "-t", "+1", "-m", "Hello", "-p", "secret", "--wait",
                "--wait-timeout=inf"};
    }
    SECTION("--wait-timeout nan") {
        args = {"send-sms", "-t", "+1", "-m", "Hello", "-p", "secret", "--wait",
                "--wait-timeout=nan"};
    }

    const RunResult r = run_send(args, harness);

    CHECK(r.code == 2);
    CHECK(harness.transport.requests.empty());
    CHECK_FALSE(r.err.empty());
}

TEST_CASE_METHOD(SmsFixture, "A router login error maps to exit 4 with its code on err",
                 "[send_sms]") {
    harness.transport.script.push_back(FakeTransport::home_page({"t1"}));
    harness.transport.script.push_back(FakeTransport::xml(kLoggedOut));
    harness.transport.script.push_back(FakeTransport::error(108006));

    const RunResult r = run_send({"send-sms", "-t", "+1", "-m", "Hello", "-p", "wrong"}, harness);

    CHECK(r.code == 4);
    CHECK_THAT(r.err, ContainsSubstring("[api_login_username_pwd_wrong]"));
    REQUIRE(harness.transport.requests.size() == 3);
}

TEST_CASE_METHOD(SmsFixture, "A router error on send-sms maps to exit 5", "[send_sms]") {
    script_login(harness.transport);
    harness.transport.script.push_back(FakeTransport::error(113004));

    const RunResult r = run_send({"send-sms", "-t", "+1", "-m", "Hello", "-p", "secret"}, harness);

    CHECK(r.code == 5);
    CHECK_FALSE(r.err.empty());
}

TEST_CASE_METHOD(SmsFixture, "A send-sms answer that is not OK maps to exit 5", "[send_sms]") {
    script_login(harness.transport);
    harness.transport.script.push_back(FakeTransport::xml("<response>FAIL</response>"));

    const RunResult r = run_send({"send-sms", "-t", "+1", "-m", "Hello", "-p", "secret"}, harness);

    CHECK(r.code == 5);
    CHECK_FALSE(r.err.empty());
}

TEST_CASE_METHOD(SmsFixture, "With --json a sent SMS prints the exact JSON object", "[send_sms]") {
    script_login(harness.transport);
    harness.transport.script.push_back(FakeTransport::xml(kOk));

    const RunResult r =
        run_send({"--json", "send-sms", "-t", "+48123456789", "-m", "Hello", "-p", "secret"},
                 harness);

    CHECK(r.code == 0);
    CHECK(r.out == R"({"status":"sent","numbers":["+48123456789"]})"
                   "\n");
    CHECK(r.err.empty());
}

TEST_CASE_METHOD(SmsFixture, "Waiting reports delivery after one 500 ms sleep", "[send_sms]") {
    script_login(harness.transport);
    harness.transport.script.push_back(FakeTransport::xml(kOk));
    harness.transport.script.push_back(status(1, 0));
    harness.transport.script.push_back(status(1, 1, "+48123456789;"));

    const RunResult r =
        run_send({"send-sms", "-t", "+48123456789", "-m", "Hello", "--wait", "-p", "secret"},
                 harness);

    CHECK(r.code == 0);
    CHECK(r.out == "SMS delivered to +48123456789\n");
    CHECK(r.err.empty());
    REQUIRE(harness.transport.requests.size() == 6);
    CHECK(harness.transport.requests[4].path == "api/sms/send-status");
    CHECK(harness.transport.requests[5].path == "api/sms/send-status");
    REQUIRE(sleeps.size() == 1);
    CHECK(sleeps[0] == milliseconds(500));
}

TEST_CASE_METHOD(SmsFixture, "Waiting with every number failed exits 6 with status failed",
                 "[send_sms]") {
    script_login(harness.transport);
    harness.transport.script.push_back(FakeTransport::xml(kOk));
    harness.transport.script.push_back(status(1, 0));
    harness.transport.script.push_back(status(1, 1, "", "+48123456789"));

    const RunResult r = run_send(
        {"--json", "send-sms", "-t", "+48123456789", "-m", "Hello", "--wait", "-p", "secret"},
        harness);

    CHECK(r.code == 6);
    CHECK(r.out == R"({"status":"failed","numbers":["+48123456789"],"succeeded":[],)"
                   R"("failed":["+48123456789"]})"
                   "\n");
}

TEST_CASE_METHOD(SmsFixture, "Waiting with some numbers failed exits 6 as partial or failed",
                 "[send_sms]") {
    script_login(harness.transport);
    harness.transport.script.push_back(FakeTransport::xml(kOk));
    harness.transport.script.push_back(status(2, 0));
    harness.transport.script.push_back(status(2, 2, "+1;", "+2;"));

    SECTION("JSON status is partial") {
        const RunResult r = run_send(
            {"--json", "send-sms", "-t", "+1,+2", "-m", "Hello", "--wait", "-p", "secret"},
            harness);
        CHECK(r.code == 6);
        CHECK_THAT(r.out, ContainsSubstring(R"("status":"partial")"));
        CHECK_THAT(r.out, ContainsSubstring(R"("succeeded":["+1"])"));
        CHECK_THAT(r.out, ContainsSubstring(R"("failed":["+2"])"));
    }
    SECTION("text names only the failed numbers") {
        const RunResult r = run_send(
            {"send-sms", "-t", "+1,+2", "-m", "Hello", "--wait", "-p", "secret"}, harness);
        CHECK(r.code == 6);
        CHECK(r.out == "SMS failed for +2\n");
    }
}

TEST_CASE_METHOD(SmsFixture, "Waiting gives up after the timeout with exit 5", "[send_sms]") {
    script_login(harness.transport);
    harness.transport.script.push_back(FakeTransport::xml(kOk));
    harness.transport.fallback = status(1, 0);

    const RunResult r = run_send({"send-sms", "-t", "+48123456789", "-m", "Hello", "--wait",
                                  "--wait-timeout", "0.05", "-p", "secret"},
                                 harness);

    CHECK(r.code == 5);
    CHECK_THAT(r.err, ContainsSubstring("[sms_send_timeout]"));
    REQUIRE(sleeps.size() >= 2);
    CHECK(sleeps[0] == milliseconds(500));
}

TEST_CASE_METHOD(SmsFixture, "Comma and semicolon both separate SucPhone and FailPhone entries",
                 "[send_sms]") {
    script_login(harness.transport);
    harness.transport.script.push_back(FakeTransport::xml(kOk));
    harness.transport.script.push_back(status(4, 4, "+1;+3,+4", "+2,"));

    SECTION("text names the failed number") {
        const RunResult r = run_send(
            {"send-sms", "-t", "+1,+2,+3,+4", "-m", "Hello", "--wait", "-p", "secret"}, harness);
        CHECK(r.code == 6);
        CHECK(r.out == "SMS failed for +2\n");
    }
    SECTION("JSON lists succeeded and failed numbers") {
        const RunResult r = run_send(
            {"--json", "send-sms", "-t", "+1,+2,+3,+4", "-m", "Hello", "--wait", "-p", "secret"},
            harness);
        CHECK(r.code == 6);
        CHECK(r.out == R"({"status":"partial","numbers":["+1","+2","+3","+4"],)"
                       R"("succeeded":["+1","+3","+4"],"failed":["+2"]})"
                       "\n");
    }
}

TEST_CASE_METHOD(SmsFixture, "Waiting delivered with comma-separated SucPhone names every number",
                 "[send_sms]") {
    script_login(harness.transport);
    harness.transport.script.push_back(FakeTransport::xml(kOk));
    harness.transport.script.push_back(status(2, 2, "+1,+2"));

    const RunResult r = run_send({"send-sms", "-t", "+1,+2", "-m", "Hello", "--wait", "-p",
                                  "secret"},
                                 harness);

    CHECK(r.code == 0);
    CHECK(r.out == "SMS delivered to +1, +2\n");
}

TEST_CASE_METHOD(SmsFixture, "JSON with --wait success prints the delivered object", "[send_sms]") {
    script_login(harness.transport);
    harness.transport.script.push_back(FakeTransport::xml(kOk));
    harness.transport.script.push_back(status(1, 1, "+48123456789;"));

    const RunResult r = run_send(
        {"--json", "send-sms", "-t", "+48123456789", "-m", "Hello", "--wait", "-p", "secret"},
        harness);

    CHECK(r.code == 0);
    CHECK(r.out == R"({"status":"delivered","numbers":["+48123456789"],)"
                   R"("succeeded":["+48123456789"],"failed":[]})"
                   "\n");
}
