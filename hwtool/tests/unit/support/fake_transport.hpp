#pragma once

#include <deque>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "hw/errors.hpp"
#include "hw/transport.hpp"

// Scripted transport for tests. Each send() pops the next scripted entry: a response is returned,
// a RequestError is thrown. Every request is recorded in `requests`.
class FakeTransport : public hw::ITransport {
public:
    std::deque<std::variant<hw::HttpResponse, hw::RequestError>> script;
    std::vector<hw::HttpRequest> requests;

    hw::HttpResponse send(const hw::HttpRequest& request) override {
        requests.push_back(request);
        if (script.empty()) {
            throw std::logic_error("unexpected request: " + request.path);
        }
        auto entry = std::move(script.front());
        script.pop_front();
        if (auto* error = std::get_if<hw::RequestError>(&entry)) {
            throw *error;
        }
        return std::get<hw::HttpResponse>(entry);
    }

    // Response with a 200 status and the given XML body.
    static hw::HttpResponse xml(std::string body, hw::Headers headers = {}) {
        hw::HttpResponse response;
        response.body = std::move(body);
        response.headers = std::move(headers);
        return response;
    }

    // Router error body, e.g. error(125001) -> <error><code>125001</code>...</error>.
    static hw::HttpResponse error(int code) {
        return xml("<error><code>" + std::to_string(code) + "</code><message></message></error>");
    }

    // Home page with one csrf_token meta tag per token, in order.
    static hw::HttpResponse home_page(const std::vector<std::string>& tokens) {
        std::string html = "<!DOCTYPE html><html><head><title>Home</title>";
        for (const auto& token : tokens) {
            html += "<meta name=\"csrf_token\" content=\"" + token + "\"/>";
        }
        html += "</head><body></body></html>";
        hw::HttpResponse response;
        response.body = std::move(html);
        return response;
    }
};
