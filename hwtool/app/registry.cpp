#include "registry.hpp"

// Step 6a stub: inert until the real implementation lands.

namespace hwtool {

void CommandRegistry::attach(CLI::App&) {}

Command* CommandRegistry::selected() const {
    return nullptr;
}

}  // namespace hwtool
