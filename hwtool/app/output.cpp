#include "output.hpp"

#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace hwtool {

namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

// Renders a JSON string literal, quotes included.
std::string quoted(std::string_view s) {
    return "\"" + json_escape(s) + "\"";
}

// Renders one field value as JSON.
std::string render_value(const Value& value) {
    return std::visit(
        [](const auto& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return quoted(v);
            } else if constexpr (std::is_same_v<T, long long>) {
                return std::to_string(v);
            } else if constexpr (std::is_same_v<T, bool>) {
                return v ? "true" : "false";
            } else {
                std::string list = "[";
                for (std::size_t i = 0; i < v.size(); ++i) {
                    if (i > 0) {
                        list += ",";
                    }
                    list += quoted(v[i]);
                }
                return list + "]";
            }
        },
        value);
}

}  // namespace

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += kHexDigits[c >> 4];
                    out += kHexDigits[c & 0x0F];
                } else {
                    out += ch;
                }
                break;
        }
    }
    return out;
}

Output::Output(std::ostream& out, std::ostream& err, bool json)
    : out_(out), err_(err), json_(json) {}

bool Output::json() const noexcept {
    return json_;
}

void Output::result(std::string_view summary, const std::vector<Field>& fields) {
    if (!json_) {
        out_ << summary << "\n";
        return;
    }
    out_ << "{";
    bool first = true;
    for (const Field& field : fields) {
        if (!first) {
            out_ << ",";
        }
        first = false;
        out_ << quoted(field.key) << ":" << render_value(field.value);
    }
    out_ << "}\n";
}

void Output::error(std::string_view code, std::string_view message) {
    if (!json_) {
        err_ << "hwtool: error: " << message << " [" << code << "]\n";
        return;
    }
    err_ << R"({"error":{"code":)" << quoted(code) << R"(,"message":)" << quoted(message)
         << "}}\n";
}

void Output::log(std::string_view line) {
    err_ << "hwtool: " << line << "\n";
}

}  // namespace hwtool
