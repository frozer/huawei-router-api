#include "hw/api/admin.hpp"

namespace hw::api {

// Inert stub: implemented in a later step.
Admin::Admin(Session& session) : session_(session) {}

LoginState Admin::state() {
    return LoginState{};
}

bool Admin::is_logged_in() {
    return false;
}

void Admin::login(const Credentials&) {}

void Admin::ensure_logged_in(const Credentials&) {}

void Admin::login_with(const Credentials&, const LoginState&) {}

}  // namespace hw::api
