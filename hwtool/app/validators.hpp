#pragma once

#include <CLI/CLI.hpp>

#include <cmath>
#include <cstddef>
#include <exception>
#include <string>

namespace hwtool {

// CLI11 validator for durations in seconds: accepts only finite numbers greater than zero.
// CLI::PositiveNumber alone lets nan through.
inline CLI::Validator finite_positive() {
    return CLI::Validator{
        [](std::string& input) -> std::string {
            std::size_t used = 0;
            double value = 0.0;
            try {
                value = std::stod(input, &used);
            } catch (const std::exception&) {
                return "must be a number greater than 0";
            }
            if (used != input.size() || !std::isfinite(value) || value <= 0.0) {
                return "must be a finite number greater than 0";
            }
            return "";
        },
        "FINITE_POSITIVE"};
}

}  // namespace hwtool
