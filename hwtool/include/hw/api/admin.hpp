#pragma once

#include <string>

#include "hw/options.hpp"
#include "hw/session.hpp"

namespace hw::api {

struct LoginState {
    int state = -1;          // 0 = logged in, -1 = logged out
    std::string username;
    int password_type = 0;   // 4 = SHA-256 token scheme
};

// Login and login-state endpoints, mirroring src/admin.js.
class Admin {
public:
    explicit Admin(Session& session);
    LoginState state();                              // GET api/user/state-login
    bool is_logged_in();                             // state().state == 0
    // Standalone: refresh_tokens, state, POST login.
    void login(const Credentials& credentials);
    void ensure_logged_in(const Credentials& credentials);  // state once; login only if needed

private:
    void login_with(const Credentials& credentials, const LoginState& state);
    Session& session_;
};

}  // namespace hw::api
