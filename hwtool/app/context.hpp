#pragma once

#include <functional>
#include <memory>
#include <string>

#include "hw/options.hpp"
#include "hw/session.hpp"
#include "hw/transport.hpp"
#include "output.hpp"

namespace hwtool {

struct GlobalOptions {
    std::string url = "http://192.168.8.1";
    std::string username = "admin";
    std::string password;
    double timeout = 10.0;  // seconds
    bool verbose = false;
    bool json = false;
};

using TransportFactory = std::function<std::unique_ptr<hw::ITransport>(const GlobalOptions&)>;

// Per-invocation state shared by the command and the framework.
class Context {
public:
    Context(GlobalOptions options, Output& output, TransportFactory factory);
    // Not copyable or movable: the session's log sink captures this.
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    const GlobalOptions& options() const noexcept;
    Output& output() noexcept;
    // Calls factory(options) once, lazily, and builds a hw::Session on the transport. When
    // verbose is set, the session's LogSink forwards each line to output.log().
    hw::Session& session();
    // Throws std::invalid_argument("password required: use -p or HWTOOL_PASSWORD") if the
    // password is empty.
    hw::Credentials credentials() const;

private:
    GlobalOptions options_;
    Output& output_;
    TransportFactory factory_;
    std::unique_ptr<hw::ITransport> transport_;
    std::unique_ptr<hw::Session> session_;
};

}  // namespace hwtool
