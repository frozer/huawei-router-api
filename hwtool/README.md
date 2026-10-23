# hwtool

`hwtool` is a native command-line tool for Huawei LTE routers. It talks to the router's HTTP API and is built as a static library (`hwapi`), a command-line executable (`hwtool`), and Catch2 unit tests.

## Requirements

- CMake 3.20 or newer
- A C++20 compiler (GCC 10+ or Clang 12+)
- libcurl development headers

On Debian/Ubuntu:

```sh
sudo apt install cmake ninja-build pkg-config libcurl4-openssl-dev
```

## Build and test

```sh
cmake -S hwtool -B hwtool/build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DHWTOOL_WERROR=ON
cmake --build hwtool/build
ctest --test-dir hwtool/build --output-on-failure
```

Dependencies (pugixml, CLI11, Catch2) are fetched automatically with CMake FetchContent.

Options: `HWTOOL_BUILD_TESTS` (default ON), `HWTOOL_WERROR` (default OFF), `HWTOOL_STATIC` (default OFF; links the executable with static libstdc++/libgcc).
