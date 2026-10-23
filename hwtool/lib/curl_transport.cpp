#include "hw/curl_transport.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "hw/errors.hpp"
#include "hw/transport.hpp"
#include "hw/version.hpp"

namespace hw {

namespace {

constexpr long kMaxRedirects = 5;
constexpr std::string_view kHttp = "http://";
constexpr std::string_view kHttps = "https://";
constexpr std::string_view kWhitespace = " \t\r\n";

struct EasyDeleter {
    void operator()(CURL* handle) const noexcept { curl_easy_cleanup(handle); }
};

struct SlistDeleter {
    void operator()(curl_slist* list) const noexcept { curl_slist_free_all(list); }
};

using EasyHandle = std::unique_ptr<CURL, EasyDeleter>;
using HeaderList = std::unique_ptr<curl_slist, SlistDeleter>;

// What the curl callbacks gather for one request.
struct Collected {
    std::string body;
    Headers headers;
};

// Turns a failed curl_easy_setopt into an error. Failing options are not silently ignored.
void check(CURLcode code) {
    if (code != CURLE_OK) {
        throw RequestError("error", curl_easy_strerror(code));
    }
}

// libcurl must be initialised once per process. The first result is cached and reused.
CURLcode global_init() {
    static const CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
    return code;
}

bool starts_with_ci(std::string_view text, std::string_view lowercase_prefix) {
    if (text.size() < lowercase_prefix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lowercase_prefix.size(); ++i) {
        const auto c = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
        if (c != lowercase_prefix[i]) {
            return false;
        }
    }
    return true;
}

std::string_view trim(std::string_view s) {
    const auto first = s.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(kWhitespace);
    return s.substr(first, last - first + 1);
}

// Checks the scheme and host, then returns the URL without trailing '/'.
std::string validated_base(const std::string& base_url) {
    std::string_view rest;
    if (starts_with_ci(base_url, kHttp)) {
        rest = std::string_view(base_url).substr(kHttp.size());
    } else if (starts_with_ci(base_url, kHttps)) {
        rest = std::string_view(base_url).substr(kHttps.size());
    } else {
        throw RouterError("invalid_router_url", base_url);
    }
    if (rest.substr(0, rest.find('/')).empty()) {
        throw RouterError("invalid_router_url", base_url);
    }
    std::string base = base_url;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base;
}

// Callbacks run inside libcurl's C frames, so no C++ exception may escape them. Returning 0
// makes curl abort the transfer with CURLE_WRITE_ERROR, which maps to "error".
std::size_t on_body(char* data, std::size_t size, std::size_t count, void* user) {
    auto* collected = static_cast<Collected*>(user);
    try {
        collected->body.append(data, size * count);
        return size * count;
    } catch (...) {
        return 0;
    }
}

// Keeps only the headers of the final response: a new "HTTP/" status line starts over.
std::size_t on_header(char* data, std::size_t size, std::size_t count, void* user) {
    auto* collected = static_cast<Collected*>(user);
    const std::size_t length = size * count;
    try {
        const std::string_view line(data, length);
        if (line.starts_with("HTTP/")) {
            collected->headers.clear();
            return length;
        }
        const auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            return length;
        }
        const std::string_view name = trim(line.substr(0, colon));
        if (name.empty()) {
            return length;
        }
        collected->headers[std::string(name)] = std::string(trim(line.substr(colon + 1)));
        return length;
    } catch (...) {
        return 0;
    }
}

bool is_no_response(CURLcode code) {
    switch (code) {
        case CURLE_COULDNT_CONNECT:
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_GOT_NOTHING:
        case CURLE_RECV_ERROR:
        case CURLE_SEND_ERROR:
            return true;
        default:
            return false;
    }
}

RequestError transport_error(CURLcode code) {
    if (code == CURLE_OPERATION_TIMEDOUT) {
        return RequestError("timeout", "HTTP request timed out");
    }
    const std::string text = curl_easy_strerror(code);
    if (is_no_response(code)) {
        return RequestError("no_response", text);
    }
    return RequestError("error", text);
}

}  // namespace

struct CurlTransport::Impl {
    std::string base;
    EasyHandle easy;
};

CurlTransport::CurlTransport(std::string base_url, std::chrono::milliseconds timeout)
    : impl_(std::make_unique<Impl>()) {
    impl_->base = validated_base(base_url);

    const CURLcode init = global_init();
    if (init != CURLE_OK) {
        throw RequestError("error", curl_easy_strerror(init));
    }
    impl_->easy.reset(curl_easy_init());
    if (!impl_->easy) {
        throw RequestError("error", "curl_easy_init failed");
    }

    CURL* handle = impl_->easy.get();
    // Clamp to the range of curl's long options (32 bits on some platforms).
    using Rep = std::chrono::milliseconds::rep;
    const Rep requested = std::max<Rep>(1, timeout.count());
    const Rep limit = static_cast<Rep>(std::numeric_limits<long>::max());
    const long millis = static_cast<long>(std::min<Rep>(requested, limit));
    check(curl_easy_setopt(handle, CURLOPT_COOKIEFILE, ""));  // in-memory cookie engine
    check(curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L));
    check(curl_easy_setopt(handle, CURLOPT_MAXREDIRS, kMaxRedirects));
    check(curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, millis));
    check(curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, millis));
    check(curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L));
    // CURLOPT_PROTOCOLS_STR is an enum value, so the version macro decides (7.85.0 introduced it).
#if LIBCURL_VERSION_NUM >= 0x075500
    check(curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "http,https"));
    check(curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "http,https"));
#else
    check(curl_easy_setopt(handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS));
    check(curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    const std::string agent = "hwtool/" + std::string(version());
    check(curl_easy_setopt(handle, CURLOPT_USERAGENT, agent.c_str()));
    check(curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, on_body));
    check(curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, on_header));
}

CurlTransport::~CurlTransport() = default;

HttpResponse CurlTransport::send(const HttpRequest& request) {
    CURL* handle = impl_->easy.get();
    const std::string url = impl_->base + "/" + request.path;
    check(curl_easy_setopt(handle, CURLOPT_URL, url.c_str()));

    // Each request sets the method explicitly, so a previous POST does not leak into a GET.
    if (request.method == "POST") {
        check(curl_easy_setopt(handle, CURLOPT_POST, 1L));
        check(curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE,
                               static_cast<curl_off_t>(request.body.size())));
        check(curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.body.c_str()));
    } else {
        check(curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L));
    }

    HeaderList list;
    for (const auto& [name, value] : request.headers) {
        const std::string line = name + ": " + value;
        curl_slist* appended = curl_slist_append(list.get(), line.c_str());
        if (appended == nullptr) {
            throw RequestError("error", "out of memory");
        }
        list.release();
        list.reset(appended);
    }
    check(curl_easy_setopt(handle, CURLOPT_HTTPHEADER, list.get()));

    Collected collected;
    check(curl_easy_setopt(handle, CURLOPT_WRITEDATA, &collected));
    check(curl_easy_setopt(handle, CURLOPT_HEADERDATA, &collected));
    const CURLcode code = curl_easy_perform(handle);
    // The header list is freed when this function returns, so detach it from the handle.
    check(curl_easy_setopt(handle, CURLOPT_HTTPHEADER, static_cast<curl_slist*>(nullptr)));
    if (code != CURLE_OK) {
        throw transport_error(code);
    }

    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    if (status < 200 || status > 399) {
        throw RequestError("invalid_status", "HTTP status " + std::to_string(status));
    }

    HttpResponse response;
    response.status = static_cast<int>(status);
    response.body = std::move(collected.body);
    response.headers = std::move(collected.headers);
    return response;
}

}  // namespace hw
