add_executable(hwapi_tests
    unit/version_test.cpp
    unit/crypto_test.cpp
    unit/utf_test.cpp
    unit/errors_test.cpp
    unit/xml_test.cpp
    unit/transport_test.cpp
    unit/session_test.cpp
    unit/admin_test.cpp
    unit/sms_test.cpp)
target_link_libraries(hwapi_tests
    PRIVATE
        hwapi
        Catch2::Catch2WithMain
        hwtool_warnings)
target_include_directories(hwapi_tests PRIVATE unit)

include(Catch)
catch_discover_tests(hwapi_tests)

add_test(NAME cli_version COMMAND hwtool --version)
set_tests_properties(cli_version PROPERTIES
    PASS_REGULAR_EXPRESSION "hwtool 0\\.1\\.0")
