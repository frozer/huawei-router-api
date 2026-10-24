#pragma once

#include <string>
#include <string_view>

namespace hw::util {

// Returns the lowercase hexadecimal SHA-256 digest of data (64 characters).
std::string sha256_hex(std::string_view data);

// Standard base64 alphabet with '=' padding.
std::string base64_encode(std::string_view data);

// base64_encode(sha256_hex(username + base64_encode(sha256_hex(password)) + token)),
// matching the login scheme in src/admin.js.
std::string encode_login_password(std::string_view username, std::string_view password,
                                  std::string_view token);

}  // namespace hw::util
