#pragma once

#include <ostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace hwtool {

using Value = std::variant<std::string, long long, bool, std::vector<std::string>>;

struct Field {
    std::string key;
    Value value;
};

// Escapes s for use inside a JSON string literal. The surrounding quotes are not added.
std::string json_escape(std::string_view s);

// Writes command results to out and diagnostics to err, as text or as one-line JSON.
class Output {
public:
    Output(std::ostream& out, std::ostream& err, bool json);

    bool json() const noexcept;
    // Text: summary + "\n" to out. JSON: one-line object + "\n" to out, fields in the given order.
    void result(std::string_view summary, const std::vector<Field>& fields);
    // Text: "hwtool: error: <message> [<code>]\n" to err.
    // JSON: {"error":{"code":"<code>","message":"<message>"}}\n to err.
    void error(std::string_view code, std::string_view message);
    // Both modes: "hwtool: <line>\n" to err.
    void log(std::string_view line);

private:
    std::ostream& out_;
    std::ostream& err_;
    bool json_;
};

}  // namespace hwtool
