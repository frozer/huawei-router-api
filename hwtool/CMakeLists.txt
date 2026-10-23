cmake_minimum_required(VERSION 3.20)

project(hwtool VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(HWTOOL_BUILD_TESTS "Build unit tests" ON)
option(HWTOOL_WERROR "Treat compiler warnings as errors" OFF)
option(HWTOOL_STATIC "Link hwtool with static libstdc++ and libgcc" OFF)

include(cmake/Dependencies.cmake)

add_library(hwtool_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hwtool_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion)
    if(HWTOOL_WERROR)
        target_compile_options(hwtool_warnings INTERFACE -Werror)
    endif()
endif()

add_library(hwapi STATIC lib/version.cpp)
target_include_directories(hwapi PUBLIC include)
target_link_libraries(hwapi
    PUBLIC
        pugixml::pugixml
        CURL::libcurl)
target_compile_definitions(hwapi PRIVATE HWTOOL_VERSION="${PROJECT_VERSION}")
target_link_libraries(hwapi PRIVATE hwtool_warnings)

add_executable(hwtool app/main.cpp)
target_include_directories(hwtool PRIVATE app)
target_link_libraries(hwtool
    PRIVATE
        hwapi
        CLI11::CLI11
        hwtool_warnings)

if(HWTOOL_STATIC)
    target_link_options(hwtool PRIVATE -static-libstdc++ -static-libgcc)
endif()

if(HWTOOL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
