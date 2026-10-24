#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <CLI/CLI.hpp>

#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cli.hpp"
#include "command.hpp"
#include "context.hpp"
#include "hw/errors.hpp"
#include "hw/options.hpp"
#include "hw/transport.hpp"
#include "output.hpp"
#include "registry.hpp"
#include "support/fake_transport.hpp"

using Catch::Matchers::ContainsSubstring;
using hwtool::Context;
using hwtool::GlobalOptions;

namespace {

// Hooks and recordings for ProbeCommand. Reset before every test by CliFixture.
struct ProbeState {
    std::function<int(Context&)> action;
    bool ran = false;
    bool flag = false;
    GlobalOptions seen;
    hw::Credentials credentials;
};

// Recordings for OtherCommand. Reset before every test by CliFixture.
struct OtherState {
    bool ran = false;
    bool flag = false;
};

ProbeState probe_state;
OtherState other_state;

// Test-only command registered as "probe". Records what it saw and runs probe_state.action.
class ProbeCommand : public hwtool::Command {
public:
    void configure(CLI::App& sub) override { sub.add_flag("--flag", flag_); }

    int run(Context& ctx) override {
        probe_state.ran = true;
        probe_state.seen = ctx.options();
        probe_state.flag = flag_;
        return probe_state.action ? probe_state.action(ctx) : 0;
    }

private:
    bool flag_ = false;
};

// Second test-only command, registered as "other". Has its own --flag and returns 7.
class OtherCommand : public hwtool::Command {
public:
    void configure(CLI::App& sub) override { sub.add_flag("--flag", flag_); }

    int run(Context&) override {
        other_state.ran = true;
        other_state.flag = flag_;
        return 7;
    }

private:
    bool flag_ = false;
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
    int factory_calls = 0;

    hwtool::TransportFactory factory() {
        return [this](const GlobalOptions&) -> std::unique_ptr<hw::ITransport> {
            ++factory_calls;
            return std::make_unique<ForwardTransport>(transport);
        };
    }
};

struct RunResult {
    int code;
    std::string out;
    std::string err;
};

// Runs hwtool with a registry holding "probe" and "other". "hwtool" is prepended to args.
RunResult run_probe(std::vector<const char*> args, Harness& harness) {
    hwtool::CommandRegistry registry;
    registry.add<ProbeCommand>("probe", "Probe the CLI wiring");
    registry.add<OtherCommand>("other", "Second test command");
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
        set(value);
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
    void set(const char* value) const {
        if (value != nullptr) {
            setenv(name_, value, 1);
        } else {
            unsetenv(name_);
        }
    }

    const char* name_;
    std::optional<std::string> previous_;
};

struct CliFixture {
    // Keeps HWTOOL_PASSWORD unset unless a test sets it.
    ScopedEnv env{"HWTOOL_PASSWORD", nullptr};
    Harness harness;

    CliFixture() {
        probe_state = ProbeState{};
        other_state = OtherState{};
    }
};

}  // namespace

TEST_CASE_METHOD(CliFixture, "No arguments print help listing both commands and return 0",
                 "[cli]") {
    const RunResult r = run_probe({}, harness);

    CHECK(r.code == 0);
    CHECK_THAT(r.out, ContainsSubstring("hwtool"));
    CHECK_THAT(r.out, ContainsSubstring("probe"));
    CHECK_THAT(r.out, ContainsSubstring("other"));
    CHECK(r.err.empty());
}

TEST_CASE_METHOD(CliFixture, "Version flag prints the version to out and returns 0", "[cli]") {
    const RunResult r = run_probe({"--version"}, harness);

    CHECK(r.code == 0);
    CHECK(r.out == "hwtool 0.1.0\n");
    CHECK(r.err.empty());
}

TEST_CASE_METHOD(CliFixture, "Help flag prints help to out and returns 0", "[cli]") {
    const RunResult r = run_probe({"--help"}, harness);

    CHECK(r.code == 0);
    CHECK_THAT(r.out, ContainsSubstring("Usage"));
    CHECK_THAT(r.out, ContainsSubstring("probe"));
    CHECK(r.err.empty());
}

TEST_CASE_METHOD(CliFixture, "Version flag works after the subcommand", "[cli]") {
    const RunResult r = run_probe({"probe", "--version"}, harness);

    CHECK(r.code == 0);
    CHECK(r.out == "hwtool 0.1.0\n");
    CHECK_FALSE(probe_state.ran);
}

TEST_CASE_METHOD(CliFixture, "Unknown option returns 2 with a message on err and no command run",
                 "[cli]") {
    const RunResult r = run_probe({"--bogus"}, harness);

    CHECK(r.code == 2);
    CHECK_FALSE(r.err.empty());
    CHECK(r.out.empty());
    CHECK_FALSE(probe_state.ran);
}

TEST_CASE_METHOD(CliFixture, "Unknown subcommand or extra positional returns 2 with empty out",
                 "[cli]") {
    SECTION("unknown subcommand") {
        const RunResult r = run_probe({"nosuch"}, harness);
        CHECK(r.code == 2);
        CHECK(r.out.empty());
    }
    SECTION("extra positional after a subcommand") {
        const RunResult r = run_probe({"probe", "extra"}, harness);
        CHECK(r.code == 2);
        CHECK(r.out.empty());
    }
    CHECK_FALSE(probe_state.ran);
}

// Post-red addition: setup failures are reported, not thrown.
TEST_CASE_METHOD(CliFixture, "Duplicate subcommand names return 1 with a message and do not throw",
                 "[cli]") {
    hwtool::CommandRegistry registry;
    registry.add<ProbeCommand>("probe", "First probe");
    registry.add<OtherCommand>("probe", "Second probe");
    std::ostringstream out;
    std::ostringstream err;
    std::vector<const char*> argv{"hwtool"};
    int code = -1;

    CHECK_NOTHROW(code = hwtool::run_cli(1, argv.data(), registry, harness.factory(), out, err));
    CHECK(code == 1);
    CHECK_FALSE(err.str().empty());
}

// Post-red addition: --timeout must be a finite number greater than zero.
TEST_CASE_METHOD(CliFixture, "Non-finite --timeout values return 2 and the command is not run",
                 "[cli]") {
    SECTION("nan") {
        const RunResult r = run_probe({"--timeout", "nan", "probe"}, harness);
        CHECK(r.code == 2);
    }
    SECTION("inf") {
        const RunResult r = run_probe({"--timeout", "inf", "probe"}, harness);
        CHECK(r.code == 2);
    }
    CHECK_FALSE(probe_state.ran);
}

TEST_CASE_METHOD(CliFixture, "Global options have the documented defaults", "[cli]") {
    const RunResult r = run_probe({"probe"}, harness);

    REQUIRE(probe_state.ran);
    CHECK(r.code == 0);
    CHECK(probe_state.seen.url == "http://192.168.8.1");
    CHECK(probe_state.seen.username == "admin");
    CHECK(probe_state.seen.password.empty());
    CHECK(probe_state.seen.timeout == 10.0);
    CHECK_FALSE(probe_state.seen.json);
    CHECK_FALSE(probe_state.seen.verbose);
}

TEST_CASE_METHOD(CliFixture, "Global options are accepted before and after the subcommand",
                 "[cli]") {
    SECTION("before the subcommand") {
        run_probe({"-p", "x", "--url", "http://10.0.0.1", "-u", "bob", "probe"}, harness);
    }
    SECTION("after the subcommand") {
        run_probe({"probe", "-p", "x", "--url", "http://10.0.0.1", "-u", "bob"}, harness);
    }

    REQUIRE(probe_state.ran);
    CHECK(probe_state.seen.password == "x");
    CHECK(probe_state.seen.url == "http://10.0.0.1");
    CHECK(probe_state.seen.username == "bob");
}

TEST_CASE_METHOD(CliFixture, "Long option names set every global option before the subcommand",
                 "[cli]") {
    const RunResult r = run_probe(
        {"--user", "bob", "--password", "pw", "--verbose", "--timeout", "3", "--json", "probe"},
        harness);

    REQUIRE(probe_state.ran);
    CHECK(r.code == 0);
    CHECK(probe_state.seen.username == "bob");
    CHECK(probe_state.seen.password == "pw");
    CHECK(probe_state.seen.verbose);
    CHECK(probe_state.seen.timeout == 3.0);
    CHECK(probe_state.seen.json);
}

TEST_CASE_METHOD(CliFixture, "Short and long global options work after the subcommand",
                 "[cli]") {
    const RunResult r = run_probe({"probe", "-v", "--json", "--timeout", "3"}, harness);

    REQUIRE(probe_state.ran);
    CHECK(r.code == 0);
    CHECK(probe_state.seen.verbose);
    CHECK(probe_state.seen.json);
    CHECK(probe_state.seen.timeout == 3.0);
}

TEST_CASE_METHOD(CliFixture, "HWTOOL_PASSWORD is used when -p is absent and -p overrides it",
                 "[cli]") {
    ScopedEnv password("HWTOOL_PASSWORD", "envpw");

    run_probe({"probe"}, harness);
    REQUIRE(probe_state.ran);
    CHECK(probe_state.seen.password == "envpw");

    probe_state = ProbeState{};
    run_probe({"-p", "cli", "probe"}, harness);
    REQUIRE(probe_state.ran);
    CHECK(probe_state.seen.password == "cli");
}

TEST_CASE_METHOD(CliFixture, "Timeout must be positive", "[cli]") {
    SECTION("zero") {
        const RunResult r = run_probe({"--timeout", "0", "probe"}, harness);
        CHECK(r.code == 2);
    }
    SECTION("negative") {
        const RunResult r = run_probe({"--timeout=-1", "probe"}, harness);
        CHECK(r.code == 2);
    }
    CHECK_FALSE(probe_state.ran);
}

TEST_CASE_METHOD(CliFixture, "A positive fractional timeout is stored as given", "[cli]") {
    const RunResult r = run_probe({"--timeout", "2.5", "probe"}, harness);

    REQUIRE(probe_state.ran);
    CHECK(r.code == 0);
    CHECK(probe_state.seen.timeout == 2.5);
}

TEST_CASE_METHOD(CliFixture, "A non-numeric timeout returns 2 and the command is not run",
                 "[cli]") {
    const RunResult r = run_probe({"--timeout", "abc", "probe"}, harness);

    CHECK(r.code == 2);
    CHECK_FALSE(probe_state.ran);
}

TEST_CASE_METHOD(CliFixture, "The command's own --flag is parsed", "[cli]") {
    run_probe({"probe", "--flag"}, harness);

    REQUIRE(probe_state.ran);
    CHECK(probe_state.flag);
}

TEST_CASE_METHOD(CliFixture, "Only the selected command sees its own --flag", "[cli]") {
    SECTION("probe is selected") {
        run_probe({"probe", "--flag"}, harness);
        REQUIRE(probe_state.ran);
        CHECK(probe_state.flag);
        CHECK_FALSE(other_state.ran);
        CHECK_FALSE(other_state.flag);
    }
    SECTION("other is selected") {
        run_probe({"other", "--flag"}, harness);
        REQUIRE(other_state.ran);
        CHECK(other_state.flag);
        CHECK_FALSE(probe_state.ran);
        CHECK_FALSE(probe_state.flag);
    }
}

TEST_CASE_METHOD(CliFixture, "The other command runs and probe does not, with exit 7", "[cli]") {
    const RunResult r = run_probe({"other"}, harness);

    CHECK(r.code == 7);
    CHECK(other_state.ran);
    CHECK_FALSE(probe_state.ran);
}

TEST_CASE_METHOD(CliFixture, "The probe command runs and other does not", "[cli]") {
    const RunResult r = run_probe({"probe"}, harness);

    CHECK(r.code == 0);
    CHECK(probe_state.ran);
    CHECK_FALSE(other_state.ran);
}

TEST_CASE_METHOD(CliFixture, "CommandRegistry selected() is null before and after a bare parse",
                 "[cli]") {
    hwtool::CommandRegistry registry;
    registry.add<ProbeCommand>("probe", "Probe the CLI wiring");
    registry.add<OtherCommand>("other", "Second test command");
    CLI::App app{"hwtool"};
    registry.attach(app);

    CHECK(registry.selected() == nullptr);

    std::vector<std::string> none;
    CHECK_NOTHROW(app.parse(none));
    CHECK(registry.selected() == nullptr);
}

TEST_CASE_METHOD(CliFixture, "CommandRegistry selected() is set after parsing a subcommand",
                 "[cli]") {
    hwtool::CommandRegistry registry;
    registry.add<ProbeCommand>("probe", "Probe the CLI wiring");
    registry.add<OtherCommand>("other", "Second test command");
    CLI::App app{"hwtool"};
    registry.attach(app);

    std::vector<std::string> args{"other"};
    CHECK_NOTHROW(app.parse(args));
    CHECK(registry.selected() != nullptr);
}

TEST_CASE_METHOD(CliFixture, "The command's return value is propagated", "[cli]") {
    probe_state.action = [](Context&) -> int { return 6; };

    const RunResult r = run_probe({"probe"}, harness);

    REQUIRE(probe_state.ran);
    CHECK(r.code == 6);
}

TEST_CASE_METHOD(CliFixture, "A RouterApiError from run maps to exit 4 with its code on err",
                 "[cli]") {
    probe_state.action = [](Context&) -> int { throw hw::RouterApiError(108002); };

    const RunResult r = run_probe({"probe"}, harness);

    CHECK(r.code == 4);
    CHECK_THAT(r.err, ContainsSubstring("[api_login_password_wrong]"));
}

TEST_CASE_METHOD(CliFixture, "A RequestError from run maps to exit 3 with a full text line",
                 "[cli]") {
    probe_state.action = [](Context&) -> int {
        throw hw::RequestError("timeout", "boom");
    };

    const RunResult r = run_probe({"probe"}, harness);

    CHECK(r.code == 3);
    CHECK(r.err == "hwtool: error: boom [http_request_timeout]\n");
}

TEST_CASE_METHOD(CliFixture, "An invalid_argument from run maps to exit 2 with its code on err",
                 "[cli]") {
    probe_state.action = [](Context&) -> int { throw std::invalid_argument("bad input"); };

    const RunResult r = run_probe({"probe"}, harness);

    CHECK(r.code == 2);
    CHECK_THAT(r.err, ContainsSubstring("bad input"));
    CHECK_THAT(r.err, ContainsSubstring("[invalid_argument]"));
}

TEST_CASE_METHOD(CliFixture, "A runtime_error from run maps to exit 1 with [internal] on err",
                 "[cli]") {
    probe_state.action = [](Context&) -> int { throw std::runtime_error("boom"); };

    const RunResult r = run_probe({"probe"}, harness);

    CHECK(r.code == 1);
    CHECK_THAT(r.err, ContainsSubstring("boom"));
    CHECK_THAT(r.err, ContainsSubstring("[internal]"));
}

TEST_CASE_METHOD(CliFixture, "With --json, a thrown error is the exact JSON error line on err",
                 "[cli]") {
    probe_state.action = [](Context&) -> int {
        throw hw::RouterError("xml_response_not_ok", "boom");
    };

    const RunResult r = run_probe({"--json", "probe"}, harness);

    CHECK(r.code == 5);
    CHECK(r.out.empty());
    CHECK(r.err == R"({"error":{"code":"xml_response_not_ok","message":"boom"}})"
                   "\n");
}

TEST_CASE_METHOD(CliFixture, "credentials() throws without a password and exit code is 2",
                 "[cli]") {
    probe_state.action = [](Context& ctx) -> int {
        ctx.credentials();
        return 0;
    };

    const RunResult r = run_probe({"probe"}, harness);

    CHECK(r.code == 2);
    CHECK_THAT(r.err, ContainsSubstring("HWTOOL_PASSWORD"));
}

TEST_CASE_METHOD(CliFixture, "credentials() returns the username and password when set",
                 "[cli]") {
    probe_state.action = [](Context& ctx) -> int {
        probe_state.credentials = ctx.credentials();
        return 0;
    };

    const RunResult r = run_probe({"-p", "secret", "-u", "bob", "probe"}, harness);

    CHECK(r.code == 0);
    CHECK(probe_state.credentials.username == "bob");
    CHECK(probe_state.credentials.password == "secret");
}

TEST_CASE_METHOD(CliFixture, "The factory is not called when the command never uses session()",
                 "[cli]") {
    const RunResult r = run_probe({"probe"}, harness);

    REQUIRE(probe_state.ran);
    CHECK(r.code == 0);
    CHECK(harness.factory_calls == 0);
}

TEST_CASE_METHOD(CliFixture, "The factory is called exactly once when session() is used twice",
                 "[cli]") {
    probe_state.action = [](Context& ctx) -> int {
        ctx.session();
        ctx.session();
        return 0;
    };

    const RunResult r = run_probe({"probe"}, harness);

    REQUIRE(probe_state.ran);
    CHECK(r.code == 0);
    CHECK(harness.factory_calls == 1);
}

TEST_CASE_METHOD(CliFixture, "With -v, session requests are logged to err", "[cli]") {
    harness.transport.script.push_back(FakeTransport::home_page({"a"}));
    harness.transport.script.push_back(FakeTransport::xml("<response>OK</response>"));
    probe_state.action = [](Context& ctx) -> int {
        ctx.session().get("api/monitoring/status");
        return 0;
    };

    const RunResult r = run_probe({"-v", "probe"}, harness);

    REQUIRE(probe_state.ran);
    CHECK(r.code == 0);
    CHECK_THAT(r.err, ContainsSubstring("hwtool: GET html/home.html -> 200\n"));
    CHECK_THAT(r.err, ContainsSubstring("hwtool: GET api/monitoring/status -> 200\n"));
}

TEST_CASE_METHOD(CliFixture, "With --json -v, logs go to err and out stays empty", "[cli]") {
    harness.transport.script.push_back(FakeTransport::home_page({"a"}));
    harness.transport.script.push_back(FakeTransport::xml("<response>OK</response>"));
    probe_state.action = [](Context& ctx) -> int {
        ctx.session().get("api/monitoring/status");
        return 0;
    };

    const RunResult r = run_probe({"--json", "-v", "probe"}, harness);

    REQUIRE(probe_state.ran);
    CHECK(r.code == 0);
    CHECK(r.out.empty());
    CHECK_THAT(r.err, ContainsSubstring("hwtool: GET html/home.html -> 200\n"));
    CHECK_THAT(r.err, ContainsSubstring("hwtool: GET api/monitoring/status -> 200\n"));
}

TEST_CASE_METHOD(CliFixture, "Without -v, session requests are not logged", "[cli]") {
    harness.transport.script.push_back(FakeTransport::home_page({"a"}));
    harness.transport.script.push_back(FakeTransport::xml("<response>OK</response>"));
    probe_state.action = [](Context& ctx) -> int {
        ctx.session().get("api/monitoring/status");
        return 0;
    };

    const RunResult r = run_probe({"probe"}, harness);

    REQUIRE(probe_state.ran);
    CHECK(r.code == 0);
    CHECK(r.err.empty());
}
