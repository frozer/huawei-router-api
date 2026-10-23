#include "registry.hpp"

#include <CLI/CLI.hpp>

#include <memory>
#include <utility>

namespace hwtool {

void CommandRegistry::attach(CLI::App& app) {
    bound_.clear();
    for (const Entry& entry : entries_) {
        CLI::App* sub = app.add_subcommand(entry.name, entry.description);
        std::unique_ptr<Command> command = entry.make();
        command->configure(*sub);
        bound_.push_back(Bound{sub, std::move(command)});
    }
}

Command* CommandRegistry::selected() const {
    for (const Bound& bound : bound_) {
        if (bound.sub->parsed()) {
            return bound.command.get();
        }
    }
    return nullptr;
}

}  // namespace hwtool
