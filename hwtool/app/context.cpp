#include "context.hpp"

#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

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
    if (!session_) {
        transport_ = factory_(options_);
        if (!transport_) {
            throw std::runtime_error("transport factory returned null");
        }
        hw::LogSink sink;
        if (options_.verbose) {
            sink = [this](std::string_view line) { output_.log(line); };
        }
        session_ = std::make_unique<hw::Session>(*transport_, std::move(sink));
    }
    return *session_;
}

hw::Credentials Context::credentials() const {
    if (options_.password.empty()) {
        throw std::invalid_argument("password required: use -p or HWTOOL_PASSWORD");
    }
    return hw::Credentials{options_.username, options_.password};
}

}  // namespace hwtool
