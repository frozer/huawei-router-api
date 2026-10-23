#include "output.hpp"

// Step 6a stub: inert until the real implementation lands.

namespace hwtool {

std::string json_escape(std::string_view) {
    return "";
}

Output::Output(std::ostream& out, std::ostream& err, bool json)
    : out_(out), err_(err), json_(json) {}

bool Output::json() const noexcept {
    return json_;
}

void Output::result(std::string_view, const std::vector<Field>&) {}

void Output::error(std::string_view, std::string_view) {}

void Output::log(std::string_view) {}

}  // namespace hwtool
