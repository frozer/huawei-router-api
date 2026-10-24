#include "hw/transport.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace hw {

namespace {

// ASCII-only folding. The value is read as unsigned char so bytes >= 0x80 are never negative.
unsigned char fold_ascii(char c) {
    const auto u = static_cast<unsigned char>(c);
    if (u >= 'A' && u <= 'Z') {
        return static_cast<unsigned char>(u - 'A' + 'a');
    }
    return u;
}

}  // namespace

bool CaseInsensitiveLess::operator()(std::string_view a, std::string_view b) const noexcept {
    const std::size_t common = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < common; ++i) {
        const unsigned char ca = fold_ascii(a[i]);
        const unsigned char cb = fold_ascii(b[i]);
        if (ca != cb) {
            return ca < cb;
        }
    }
    return a.size() < b.size();
}

}  // namespace hw
