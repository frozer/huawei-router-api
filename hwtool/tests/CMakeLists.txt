add_executable(hwapi_tests unit/version_test.cpp)
target_link_libraries(hwapi_tests
    PRIVATE
        hwapi
        Catch2::Catch2WithMain
        hwtool_warnings)

include(Catch)
catch_discover_tests(hwapi_tests)

add_test(NAME cli_version COMMAND hwtool --version)
set_tests_properties(cli_version PROPERTIES
    PASS_REGULAR_EXPRESSION "hwtool 0\\.1\\.0")
