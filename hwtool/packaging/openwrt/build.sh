#!/bin/sh
# Builds a static hwtool for little-endian MIPS OpenWrt (ramips) in Docker, runs the test suite
# under qemu-mipsel, and writes the stripped binary to hwtool/build/dist/.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../.." && pwd)
image=hwtool-openwrt-mipsel
dist="$src/build/dist"

docker build -t "$image" "$here"
mkdir -p "$dist"

docker run --rm \
    -v "$src:/src:ro" \
    -v "$dist:/out" \
    -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
    "$image" sh -euc '
        cmake -S /src -B /build -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/mipsel-openwrt.cmake \
            -DCMAKE_BUILD_TYPE=Release -DHWTOOL_WERROR=ON
        cmake --build /build
        (cd /build && ctest --output-on-failure)
        version=$(qemu-mipsel /build/hwtool --version | cut -d" " -f2)
        name=hwtool-$version-openwrt-ramips-mipsel
        mipsel-linux-muslsf-strip -o "/out/$name" /build/hwtool
        (cd /out && sha256sum "$name" > "$name.sha256")
        chown "$HOST_UID:$HOST_GID" "/out/$name" "/out/$name.sha256"
        file "/out/$name"
        ls -l "/out/$name"
    '
