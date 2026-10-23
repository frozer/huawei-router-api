#pragma once

#include <map>
#include <string>
#include <string_view>

namespace hw {

// Strict weak ordering over header names that ignores ASCII case.
struct CaseInsensitiveLess {
    using is_transparent = void;
    // ASCII case-insensitive comparison.
    bool operator()(std::string_view a, std::string_view b) const noexcept;
};

using Headers = std::map<std::string, std::string, CaseInsensitiveLess>;

struct HttpRequest {
    std::string method = "GET";
    std::string path;  // relative to router root, no leading slash, e.g. "api/user/login"
    std::string body;
    Headers headers;
};

struct HttpResponse {
    int status = 200;
    std::string body;
    Headers headers;
};

class ITransport {
public:
    virtual ~ITransport() = default;

    // Throws hw::RequestError on network failure or non-2xx/3xx status.
    virtual HttpResponse send(const HttpRequest& request) = 0;
};

}  // namespace hw
