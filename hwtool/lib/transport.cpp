#include "hw/transport.hpp"

namespace hw {

// Stub: intentionally inert so that the tests fail until the real comparator is implemented.
bool CaseInsensitiveLess::operator()(std::string_view, std::string_view) const noexcept {
    return false;
}

}  // namespace hw
