#include "hw/util/crypto.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace hw::util {

namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2,
};

constexpr std::uint32_t rotr(std::uint32_t x, unsigned int n) {
    return (x >> n) | (x << (32U - n));
}

std::uint32_t load_be32(const std::vector<unsigned char>& buf, std::size_t offset) {
    return (static_cast<std::uint32_t>(buf[offset]) << 24) |
           (static_cast<std::uint32_t>(buf[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(buf[offset + 2]) << 8) |
           static_cast<std::uint32_t>(buf[offset + 3]);
}

void compress(std::array<std::uint32_t, 8>& state, const std::vector<unsigned char>& buf,
              std::size_t offset) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = load_be32(buf, offset + 4 * i);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];

    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

}  // namespace

std::string sha256_hex(std::string_view data) {
    std::vector<unsigned char> buf;
    buf.reserve(data.size() + 72);
    for (char ch : data) {
        buf.push_back(static_cast<unsigned char>(ch));
    }

    // Padding: a 0x80 byte, zeros up to 56 mod 64, then the 64-bit big-endian bit length.
    const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8U;
    buf.push_back(0x80);
    while (buf.size() % 64 != 56) {
        buf.push_back(0x00);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        buf.push_back(static_cast<unsigned char>(bit_length >> shift));
    }

    std::array<std::uint32_t, 8> state = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    for (std::size_t offset = 0; offset < buf.size(); offset += 64) {
        compress(state, buf, offset);
    }

    constexpr std::string_view kHexDigits = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::uint32_t word : state) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            out.push_back(kHexDigits[(word >> shift) & 0xFU]);
        }
    }
    return out;
}

std::string base64_encode(std::string_view data) {
    constexpr std::string_view kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);

    std::size_t i = 0;
    while (i < data.size()) {
        const std::size_t remaining = data.size() - i;
        const auto b0 = static_cast<std::uint32_t>(static_cast<unsigned char>(data[i]));
        const auto b1 = remaining > 1
                            ? static_cast<std::uint32_t>(static_cast<unsigned char>(data[i + 1]))
                            : 0U;
        const auto b2 = remaining > 2
                            ? static_cast<std::uint32_t>(static_cast<unsigned char>(data[i + 2]))
                            : 0U;
        const std::uint32_t n = (b0 << 16) | (b1 << 8) | b2;

        out.push_back(kAlphabet[(n >> 18) & 0x3FU]);
        out.push_back(kAlphabet[(n >> 12) & 0x3FU]);
        out.push_back(remaining > 1 ? kAlphabet[(n >> 6) & 0x3FU] : '=');
        out.push_back(remaining > 2 ? kAlphabet[n & 0x3FU] : '=');
        i += 3;
    }
    return out;
}

std::string encode_login_password(std::string_view username, std::string_view password,
                                  std::string_view token) {
    // Mirrors src/admin.js: the password hash is base64-encoded as a hex string.
    const std::string password_hash = base64_encode(sha256_hex(password));
    const std::string combined =
        std::string(username) + password_hash + std::string(token);
    return base64_encode(sha256_hex(combined));
}

}  // namespace hw::util
