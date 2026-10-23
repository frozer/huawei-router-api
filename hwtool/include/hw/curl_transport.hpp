#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "hw/transport.hpp"

namespace hw {

class CurlTransport : public ITransport {
public:
    // base_url must start with http:// or https:// (case-insensitive) and have a non-empty host;
    // a trailing '/' is trimmed. Otherwise throws RouterError("invalid_router_url", base_url).
    CurlTransport(std::string base_url, std::chrono::milliseconds timeout);
    ~CurlTransport() override;
    CurlTransport(const CurlTransport&) = delete;
    CurlTransport& operator=(const CurlTransport&) = delete;

    // URL = base + "/" + request.path. In-memory cookie engine (cookies persist across calls),
    // follows redirects (max 5), total timeout = timeout. Response headers are those of the final
    // response. Status outside 200..399 -> RequestError("invalid_status", "HTTP status <n>").
    // Timeout -> RequestError("timeout", ...). Connect/resolve failure, empty reply, recv error ->
    // RequestError("no_response", ...). Other curl errors -> RequestError("error", <curl message>).
    HttpResponse send(const HttpRequest& request) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace hw
