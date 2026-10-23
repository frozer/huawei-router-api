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
    void attach(CLI::App& app);
    // After parse: the command whose subcommand was given, or nullptr.
    Command* selected() const;

private:
    struct Entry {
        std::string name;
        std::string description;
        std::function<std::unique_ptr<Command>()> make;
    };

    std::vector<Entry> entries_;
    std::vector<std::unique_ptr<Command>> commands_;
    Command* selected_ = nullptr;
};

}  // namespace hwtool
