#include "hw/util/utf.hpp"

#include <stdexcept>

namespace hw::util {

namespace {

// Continuation byte range and the number of trailing bytes for a sequence.
struct Sequence {
    std::size_t length;
    unsigned char second_min;
    unsigned char second_max;
};

Sequence sequence_for_lead(unsigned char lead) {
    if (lead <= 0x7F) {
        return {1, 0x00, 0x7F};
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
        return {2, 0x80, 0xBF};
    }
    if (lead == 0xE0) {
        return {3, 0xA0, 0xBF};
    }
    if (lead >= 0xE1 && lead <= 0xEC) {
        return {3, 0x80, 0xBF};
    }
    if (lead == 0xED) {
        return {3, 0x80, 0x9F};  // excludes surrogates U+D800..U+DFFF
    }
    if (lead >= 0xEE && lead <= 0xEF) {
        return {3, 0x80, 0xBF};
    }
    if (lead == 0xF0) {
        return {4, 0x90, 0xBF};  // excludes overlong encodings
    }
    if (lead >= 0xF1 && lead <= 0xF3) {
        return {4, 0x80, 0xBF};
    }
    if (lead == 0xF4) {
        return {4, 0x80, 0x8F};  // excludes code points above U+10FFFF
    }
    return {0, 0, 0};  // 0x80..0xC1 and 0xF5..0xFF are never valid leads
}

}  // namespace

std::size_t utf16_length(std::string_view utf8) {
    std::size_t units = 0;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const auto lead = static_cast<unsigned char>(utf8[i]);
        const Sequence seq = sequence_for_lead(lead);
        if (seq.length == 0) {
            throw std::invalid_argument("invalid UTF-8 lead byte");
        }
        if (i + seq.length > utf8.size()) {
            throw std::invalid_argument("truncated UTF-8 sequence");
        }
        for (std::size_t k = 1; k < seq.length; ++k) {
            const auto cont = static_cast<unsigned char>(utf8[i + k]);
            const unsigned char lo = k == 1 ? seq.second_min : 0x80;
            const unsigned char hi = k == 1 ? seq.second_max : 0xBF;
            if (cont < lo || cont > hi) {
                throw std::invalid_argument("invalid UTF-8 continuation byte");
            }
        }
        units += seq.length == 4 ? 2 : 1;
        i += seq.length;
    }
    return units;
}

}  // namespace hw::util
