#include "../settings_validation.hpp"

#include <cassert>
#include <cstring>
#include <string>
#include <string_view>

using mosaic::features::provisioning::form::parse;
using mosaic::features::provisioning::form::Settings;
using mosaic::features::provisioning::form::valid_settings;
using mosaic::features::provisioning::form::valid_wss_url;

int main() {
    Settings settings { };
    auto accepts = [&](std::string_view input) {
        assert(parse(input, settings));
        assert(valid_settings(settings));
    };
    auto rejects = [&](std::string_view input) {
        assert(!parse(input, settings));
        for (unsigned char c : std::string_view(reinterpret_cast<const char*>(&settings), sizeof(settings)))
            assert(c == 0);
    };

    accepts("ssid=Home+Wi-Fi&pass=correct%26password&ws_url=wss%3A%2F%2Fexample.org%3A8443%2Fdevice%3Fx%3D1");
    assert(std::strcmp(settings.ssid, "Home Wi-Fi") == 0);
    assert(std::strcmp(settings.pass, "correct&password") == 0);
    assert(std::strcmp(settings.ws_url, "wss://example.org:8443/device?x=1") == 0);
    accepts("ws_url=wss%3A%2F%2Fexample.org%2F&pass=&ssid=Open");
    assert(settings.pass[0] == 0);
    accepts("ssid=" + std::string(32, 'S') + "&pass=" + std::string(63, 'P') + "&ws_url=wss%3A%2F%2Fexample.org");

    // Encode every byte to exercise the transport's worst-case size, not just ASCII.
    const auto encode = [](std::string_view value) {
        constexpr char hex[] = "0123456789ABCDEF";
        std::string encoded;
        for (unsigned char c : value) {
            encoded += '%';
            encoded += hex[c >> 4];
            encoded += hex[c & 15];
        }
        return encoded;
    };
    const std::string max_ssid(32, '!');
    const std::string max_pass(63, '!');
    const std::string max_url = "wss://example.org/" + std::string(174, '!');
    const auto encoded_body = "ssid=" + encode(max_ssid) + "&pass=" + encode(max_pass) + "&ws_url=" + encode(max_url);
    assert(encoded_body.size() > 512);
    assert(encoded_body.size() <= mosaic::features::provisioning::form::kEncodedBodyLimit);
    accepts(encoded_body);
    assert(std::strcmp(settings.ssid, max_ssid.c_str()) == 0);
    assert(std::strcmp(settings.pass, max_pass.c_str()) == 0);
    assert(std::strcmp(settings.ws_url, max_url.c_str()) == 0);

    rejects("");
    rejects("ssid=abc&pass=12345678");
    rejects("ssid=abc&pass=12345678&ws_url=wss%3A%2F%2Fexample.org&ssid=other");
    rejects("ssid=abc&pass=12345678&ws_url=wss%3A%2F%2Fexample.org&admin=bad");
    rejects("ssid=abc&pass=12345678&ws_url=wss%3A%2F%2Fexample.org&");
    rejects("ssid=abc&pass=12345678&ws_url");
    rejects("ssid=abc&pass=12345678&ws_url=wss%3A%2F%2Fexample.org%2");
    rejects("ssid=abc&pass=12345678&ws_url=wss%3A%2F%2Fexample.org%GG");
    rejects("ssid=abc&pass=pass%00word&ws_url=wss%3A%2F%2Fexample.org");
    rejects("ssid=abc&pass=12345678&ws_url=wss%3A%2F%2Fexample.org%0a");
    rejects("ssid=" + std::string(33, 'S') + "&pass=12345678&ws_url=wss%3A%2F%2Fexample.org");
    rejects("ssid=abc&pass=" + std::string(64, 'P') + "&ws_url=wss%3A%2F%2Fexample.org");
    rejects("ssid=abc&pass=short&ws_url=wss%3A%2F%2Fexample.org");
    rejects("ssid=abc&pass=12345678&ws_url=ws%3A%2F%2Fexample.org");
    rejects("ssid=abc&pass=12345678&ws_url=wss%3A%2F%2Fuser%40example.org");
    rejects("ssid=abc&pass=12345678&ws_url=wss%3A%2F%2Fexample.org%3A65536");
    rejects("ssid=abc&pass=12345678&ws_url=wss%3A%2F%2Fexample.org%23fragment");
    rejects("ssid=abc&pass=12345678&ws_url=" + std::string(193, 'x'));
    assert(valid_wss_url("wss://example.org:443/path"));
    assert(!valid_wss_url("https://example.org"));
    assert(!valid_wss_url("wss://example.org:0"));

    Settings invalid { };
    std::memset(invalid.ssid, 'x', sizeof(invalid.ssid));
    assert(!valid_settings(invalid));
    assert(!valid_settings(settings)); // rejected parse clears the magic too
}
