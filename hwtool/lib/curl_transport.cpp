#include "hw/curl_transport.hpp"

#include <chrono>
#include <memory>
#include <string>

#include "hw/errors.hpp"
#include "hw/transport.hpp"

namespace hw {

struct CurlTransport::Impl {};

CurlTransport::CurlTransport(std::string, std::chrono::milliseconds) {}

CurlTransport::~CurlTransport() = default;

HttpResponse CurlTransport::send(const HttpRequest&) {
    throw RequestError("error", "not implemented");
}

}  // namespace hw
