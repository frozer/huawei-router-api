#include <CLI/CLI.hpp>

#include <iostream>
#include <string>

#include "hw/version.hpp"

int main(int argc, char** argv) {
    CLI::App app{"Command-line tool for Huawei LTE routers", "hwtool"};

    app.set_version_flag("--version", std::string("hwtool ") + std::string(hw::version()));
    app.require_subcommand(0, 1);

    if (argc < 2) {
        std::cout << app.help();
        return 0;
    }

    CLI11_PARSE(app, argc, argv);
    return 0;
}
