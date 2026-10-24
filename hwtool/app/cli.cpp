#include "cli.hpp"

#include <CLI/CLI.hpp>

#include <exception>
#include <ostream>
#include <string>
#include <utility>

#include "exit_codes.hpp"
#include "hw/version.hpp"
#include "output.hpp"
#include "validators.hpp"

namespace hwtool {

int run_cli(int argc, const char* const* argv, CommandRegistry& registry, TransportFactory factory,
            std::ostream& out, std::ostream& err) {
    GlobalOptions options;
    bool show_version = false;

    CLI::App app{"Command-line tool for Huawei LTE routers", "hwtool"};
    try {
        app.fallthrough();
        app.add_option("--url", options.url, "Router base URL")->capture_default_str();
        app.add_option("-u,--user", options.username, "Router username")->capture_default_str();
        app.add_option("-p,--password", options.password, "Router password")
            ->envname("HWTOOL_PASSWORD");
        app.add_option("--timeout", options.timeout, "Request timeout in seconds (> 0)")
            ->check(finite_positive())
            ->capture_default_str();
        app.add_flag("-v,--verbose", options.verbose, "Log HTTP requests to stderr");
        app.add_flag("--json", options.json, "Print results as JSON");
        app.add_flag("--version", show_version, "Print the version and exit");
        registry.attach(app);
    } catch (const std::exception& e) {
        err << "hwtool: " << e.what() << "\n";
        return static_cast<int>(ExitCode::failure);
    }

    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp&) {
        out << app.help();
        return 0;
    } catch (const CLI::ParseError& e) {
        err << "hwtool: " << e.what() << "\n";
        return 2;
    }

    if (show_version) {
        out << "hwtool " << hw::version() << "\n";
        return 0;
    }

    Command* command = registry.selected();
    if (command == nullptr) {
        out << app.help();
        return 0;
    }

    Output output(out, err, options.json);
    Context context(std::move(options), output, std::move(factory));
    try {
        return command->run(context);
    } catch (const std::exception& e) {
        output.error(error_code_for(e), e.what());
        return static_cast<int>(exit_code_for(e));
    } catch (...) {
        output.error("internal", "unknown error");
        return static_cast<int>(ExitCode::failure);
    }
}

}  // namespace hwtool
