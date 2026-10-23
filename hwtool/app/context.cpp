#include "context.hpp"

#include <stdexcept>
#include <utility>

// Step 6a stub: inert until the real implementation lands.

namespace hwtool {

Context::Context(GlobalOptions options, Output& output, TransportFactory factory)
    : options_(std::move(options)), output_(output), factory_(std::move(factory)) {}

const GlobalOptions& Context::options() const noexcept {
    return options_;
}

Output& Context::output() noexcept {
    return output_;
}

hw::Session& Context::session() {
    throw std::logic_error("not implemented");
}

hw::Credentials Context::credentials() const {
    return {};
}

}  // namespace hwtool
