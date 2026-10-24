#include <catch2/catch_test_macros.hpp>

#include "hw/version.hpp"

TEST_CASE("hw::version reports the project version", "[version]") {
    CHECK(hw::version() == "0.1.0");
}
