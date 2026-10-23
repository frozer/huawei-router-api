#include <catch2/catch_test_macros.hpp>

#include <string>

#include "hw/util/crypto.hpp"

TEST_CASE("sha256_hex matches known digests", "[crypto]") {
    CHECK(hw::util::sha256_hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(hw::util::sha256_hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(hw::util::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("sha256_hex handles one million bytes", "[crypto]") {
    const std::string data(1000000, 'a');
    CHECK(hw::util::sha256_hex(data) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256_hex is correct around padding boundaries", "[crypto]") {
    CHECK(hw::util::sha256_hex(std::string(55, 'a')) ==
          "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK(hw::util::sha256_hex(std::string(56, 'a')) ==
          "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK(hw::util::sha256_hex(std::string(63, 'a')) ==
          "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34");
    CHECK(hw::util::sha256_hex(std::string(64, 'a')) ==
          "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    CHECK(hw::util::sha256_hex(std::string(65, 'a')) ==
          "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0");
}

TEST_CASE("base64_encode matches RFC 4648 vectors", "[crypto]") {
    CHECK(hw::util::base64_encode("") == "");
    CHECK(hw::util::base64_encode("f") == "Zg==");
    CHECK(hw::util::base64_encode("fo") == "Zm8=");
    CHECK(hw::util::base64_encode("foo") == "Zm9v");
    CHECK(hw::util::base64_encode("foob") == "Zm9vYg==");
    CHECK(hw::util::base64_encode("fooba") == "Zm9vYmE=");
    CHECK(hw::util::base64_encode("foobar") == "Zm9vYmFy");
}

TEST_CASE("base64_encode handles binary bytes", "[crypto]") {
    const std::string data("\xff\x00\xfe", 3);
    CHECK(hw::util::base64_encode(data) == "/wD+");
}

TEST_CASE("encode_login_password matches the original JS logic", "[crypto]") {
    CHECK(hw::util::encode_login_password("admin", "secret",
                                          "TOKEN0123456789abcdef0123456789ab") ==
          "MTA2Y2U1MzgwNTE1OTcyOTk0MGZlZmU1ODU2ZTU3MDRkN2UwMDVhODhj"
          "OTU2ODQ3YzY0OWQ1Y2RmYTEyMWMyMA==");
}
