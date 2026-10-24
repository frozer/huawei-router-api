include(FetchContent)

find_package(CURL REQUIRED)

FetchContent_Declare(pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG v1.14
    GIT_SHALLOW TRUE)

FetchContent_Declare(CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
    GIT_SHALLOW TRUE)

FetchContent_MakeAvailable(pugixml CLI11)

# Provide the namespaced alias if the pugixml build only exposes a plain target.
if(NOT TARGET pugixml::pugixml AND TARGET pugixml)
    add_library(pugixml::pugixml ALIAS pugixml)
endif()

if(HWTOOL_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE)

    FetchContent_MakeAvailable(Catch2)

    # Catch2 ships the Catch.cmake module (catch_discover_tests) in extras/.
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()
