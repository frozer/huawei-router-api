#include "hw/api/admin.hpp"

#include <string>

#include "hw/errors.hpp"
#include "hw/options.hpp"
#include "hw/session.hpp"
#include "hw/util/crypto.hpp"
#include "hw/xml.hpp"
#include "../util/parse.hpp"

namespace hw::api {

namespace {

constexpr int kPasswordTypeSha256 = 4;
constexpr int kLoggedIn = 0;

}  // namespace

Admin::Admin(Session& session) : session_(session) {}

LoginState Admin::state() {
    const XmlResponse response = session_.get("api/user/state-login");
    const XmlNode& root = response.root();

    LoginState result;
    result.state = util::parse_int("State", root.text("State"));
    result.username = root.find("Username").value_or(std::string());
    // Older firmware omits password_type; an empty element is treated the same way.
    const auto password_type = root.find("password_type");
    if (password_type && !util::trim(*password_type).empty()) {
        result.password_type = util::parse_int("password_type", *password_type);
    }
    return result;
}

bool Admin::is_logged_in() {
    return state().state == kLoggedIn;
}

void Admin::login(const Credentials& credentials) {
    // Fresh tokens first, so the state call and the hash both use them, as admin.js does.
    session_.refresh_tokens();
    const LoginState current = state();
    login_with(credentials, current);
}

void Admin::ensure_logged_in(const Credentials& credentials) {
    const LoginState current = state();
    if (current.state == kLoggedIn) {
        return;
    }
    login_with(credentials, current);
}

void Admin::login_with(const Credentials& credentials, const LoginState& state) {
    const auto& tokens = session_.tokens();
    std::string password;
    if (state.password_type == kPasswordTypeSha256 && !tokens.empty()) {
        password = util::encode_login_password(credentials.username, credentials.password,
                                               tokens.front());
    } else {
        password = util::base64_encode(credentials.password);
    }

    XmlRequest body;
    body.add("Username", credentials.username)
        .add("Password", password)
        .add("password_type", static_cast<long long>(state.password_type));
    session_.post("api/user/login", body, Expect::Ok);
}

}  // namespace hw::api
