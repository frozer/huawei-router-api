#include "hw/version.hpp"

namespace hw {

std::string_view version() noexcept {
    return HWTOOL_VERSION;
}

}  // namespace hw
