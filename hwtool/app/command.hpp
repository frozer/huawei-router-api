#pragma once

namespace CLI {
class App;
}

namespace hwtool {

class Context;

// A subcommand. configure() declares its own options on the subcommand; run() executes it and
// returns the process exit code.
class Command {
public:
    virtual ~Command() = default;
    virtual void configure(CLI::App& sub) = 0;
    virtual int run(Context& ctx) = 0;
};

}  // namespace hwtool
