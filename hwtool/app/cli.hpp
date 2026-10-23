#pragma once

#include <iosfwd>

#include "context.hpp"
#include "registry.hpp"

namespace hwtool {

// Builds the CLI11 app "hwtool" (description "Command-line tool for Huawei LTE routers") with
// these global options:
//   --url (default http://192.168.8.1), -u/--user (default admin), -p/--password (falls back to
//   env HWTOOL_PASSWORD), --timeout <seconds> (default 10, must be > 0), -v/--verbose, --json,
//   --version (prints "hwtool <version>" to out, returns 0).
// app.fallthrough() is set, so global options are also accepted after the subcommand.
//   no subcommand -> help text to out, return 0;
//   CLI11 parse error -> error message to err, return 2;
//   --help -> help to out, return 0;
//   otherwise build Output(out, err, json) and Context, then call selected->run(ctx).
//   Any std::exception from run is caught: output.error(error_code_for(e), e.what()), and the
//   return value is int(exit_code_for(e)).
int run_cli(int argc, const char* const* argv, CommandRegistry& registry, TransportFactory factory,
            std::ostream& out, std::ostream& err);

}  // namespace hwtool
