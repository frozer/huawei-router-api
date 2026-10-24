# Build image for a static hwtool on little-endian MIPS OpenWrt (ramips: RT288x, RT305x,
# MT7620, MT7621). The device toolchain (OpenWrt 14.07 SDK: GCC 4.8, uClibc) cannot build C++20,
# so this builds a modern GCC + musl cross toolchain and links everything statically.
FROM debian:bookworm

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential ca-certificates cmake curl file git ninja-build pkg-config \
        python3 qemu-user rsync wget xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Soft-float MIPS32 (r1) runs on every ramips CPU; none of them has an FPU.
ARG MCM_COMMIT=5e8042e63de29f8473fc3b0b831a587ba284e1a8
ARG GCC_VER=14.3.0
ARG TARGET=mipsel-linux-muslsf
ENV CROSS=/opt/cross
RUN git clone https://github.com/richfelker/musl-cross-make.git /tmp/mcm \
    && cd /tmp/mcm \
    && git checkout "$MCM_COMMIT" \
    && printf '%s\n' \
        "TARGET = $TARGET" \
        "OUTPUT = $CROSS" \
        "GCC_VER = $GCC_VER" \
        "COMMON_CONFIG += CFLAGS=\"-g0 -O2\" CXXFLAGS=\"-g0 -O2\"" \
        "GCC_CONFIG += --with-arch=mips32 --with-float=soft --enable-languages=c,c++" \
        "GCC_CONFIG += --disable-libquadmath --disable-libsanitizer --disable-multilib" \
        > config.mak \
    && make -j"$(nproc)" install \
    && rm -rf /tmp/mcm
ENV PATH=$CROSS/bin:$PATH

# HTTP-only static libcurl: the router's web UI on the LAN is plain http, so no TLS.
ARG CURL_VER=8.14.1
ARG CURL_SHA256=f4619a1e2474c4bbfedc88a7c2191209c8334b48fa1f4e53fd584cc12e9120dd
RUN cd /tmp \
    && curl -fsSLO "https://curl.se/download/curl-$CURL_VER.tar.xz" \
    && echo "$CURL_SHA256  curl-$CURL_VER.tar.xz" | sha256sum -c - \
    && tar xf "curl-$CURL_VER.tar.xz" \
    && cd "curl-$CURL_VER" \
    && ./configure --host="$TARGET" --prefix="$CROSS/$TARGET" \
        --disable-shared --enable-static --disable-docs --disable-manual \
        --enable-http --disable-ftp --disable-file --disable-ldap --disable-ldaps \
        --disable-rtsp --disable-dict --disable-telnet --disable-tftp --disable-pop3 \
        --disable-imap --disable-smb --disable-smtp --disable-gopher --disable-mqtt \
        --disable-ipfs --disable-websockets --disable-ntlm --disable-unix-sockets \
        --without-ssl --without-zlib --without-brotli --without-zstd --without-libpsl \
        --without-libidn2 --without-nghttp2 --without-libssh2 --without-librtmp \
        --disable-threaded-resolver \
    && make -j"$(nproc)" install \
    && rm -rf /tmp/curl-*

WORKDIR /build
