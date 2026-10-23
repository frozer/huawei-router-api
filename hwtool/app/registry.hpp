#pragma once

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "command.hpp"

namespace CLI {
class App;
}

namespace hwtool {

// Ordered set of subcommands. Each registered command becomes one subcommand of the app.
class CommandRegistry {
public:
    // T must be default-constructible and derive from Command.
    template <class T>
    void add(std::string name, std::string description) {
        static_assert(std::is_base_of_v<Command, T>, "T must derive from hwtool::Command");
        entries_.push_back(Entry{
            std::move(name), std::move(description),
            [] { return std::unique_ptr<Command>(std::make_unique<T>()); }});
    }

    // One subcommand per entry, in insertion order; calls configure(sub) on each command.
    // Replaces any previous attachment, so calling it again does not duplicate subcommands.
    void attach(CLI::App& app);
    // After parse: the command whose subcommand was given, or nullptr. The pointer stays valid
    // only while the registry is alive and the CLI::App passed to attach() is alive; the
    // registry owns the command, and the subcommand pointers belong to that app.
    Command* selected() const;

private:
    struct Entry {
        std::string name;
        std::string description;
        std::function<std::unique_ptr<Command>()> make;
    };

    // A registered command once attached: its CLI11 subcommand and the owned instance.
    struct Bound {
        CLI::App* sub;
        std::unique_ptr<Command> command;
    };

    std::vector<Entry> entries_;
    std::vector<Bound> bound_;
};

}  // namespace hwtool
