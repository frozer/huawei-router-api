# Cross-compiles a fully static hwtool for little-endian MIPS OpenWrt devices (e.g. ramips).
# Expects the mipsel-linux-muslsf toolchain from packaging/openwrt/Dockerfile on PATH, with a
# static libcurl installed into its sysroot.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR mipsel)

set(HWTOOL_TARGET_TRIPLE mipsel-linux-muslsf)
set(CMAKE_C_COMPILER ${HWTOOL_TARGET_TRIPLE}-gcc)
set(CMAKE_CXX_COMPILER ${HWTOOL_TARGET_TRIPLE}-g++)

# Use the compiler's own sysroot for libraries and headers, and the build host for programs.
execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -print-sysroot
    OUTPUT_VARIABLE HWTOOL_SYSROOT
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT HWTOOL_SYSROOT)
    execute_process(
        COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libc.a
        OUTPUT_VARIABLE libc_path
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    get_filename_component(HWTOOL_SYSROOT "${libc_path}/../.." ABSOLUTE)
endif()
set(CMAKE_FIND_ROOT_PATH ${HWTOOL_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# The device's uClibc and libstdc++ are too old, so link everything in.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

# Lets ctest run the target binaries on the build host.
set(CMAKE_CROSSCOMPILING_EMULATOR qemu-mipsel)
