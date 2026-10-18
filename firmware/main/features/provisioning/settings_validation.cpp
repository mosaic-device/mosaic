#include "settings_validation.hpp"
#include "../../support/secure_erase.hpp"

#include <cstring>

namespace mosaic::features::provisioning::form {
namespace {

    bool terminated(const char* data, size_t capacity, bool required) {
        const void* end = std::memchr(data, 0, capacity);
        return end && (!required || end != data);
    }

    int hex(char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    }

    bool decode(std::string_view input, char* out, size_t capacity) {
        size_t n = 0;
        for (size_t i = 0; i < input.size(); ++i) {
            unsigned char c = input[i];
            if (c == '%') {
                if (i + 2 >= input.size() || hex(input[i + 1]) < 0 || hex(input[i + 2]) < 0)
                    return false;
                c = static_cast<unsigned char>((hex(input[i + 1]) << 4) | hex(input[i + 2]));
                i += 2;
            } else if (c == '+') {
                c = ' ';
            }
            if (c == 0 || c < 0x20 || c == 0x7f || n + 1 >= capacity)
                return false;
            out[n++] = static_cast<char>(c);
        }
        out[n] = 0;
        return true;
    }
} // namespace

bool valid_wss_url(std::string_view url) {
    if (url.size() < 7 || url.size() > 192 || url.substr(0, 6) != "wss://")
        return false;
    url.remove_prefix(6);
    const auto end = url.find_first_of("/?#");
    const auto authority = url.substr(0, end);
    if (authority.empty() || authority.find('@') != url.npos)
        return false;
    const auto colon = authority.find(':');
    const auto host = authority.substr(0, colon);
    if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.')
        return false;
    for (unsigned char c : host) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.'))
            return false;
    }
    if (host.find('.') == host.npos && host != "localhost")
        return false;
    if (colon != authority.npos) {
        const auto port = authority.substr(colon + 1);
        if (port.empty() || port.size() > 5)
            return false;
        unsigned number = 0;
        for (unsigned char c : port) {
            if (c < '0' || c > '9')
                return false;
            number = number * 10 + c - '0';
        }
        if (number == 0 || number > 65535)
            return false;
    }
    for (unsigned char c : url) {
        if (c < 0x21 || c > 0x7e || c == '#' || c == '\\')
            return false;
    }
    return true;
}

bool valid_settings(const Settings& settings) {
    if (settings.magic != kMagic || !terminated(settings.ssid, sizeof(settings.ssid), true)
        || !terminated(settings.pass, sizeof(settings.pass), false)
        || !terminated(settings.ws_url, sizeof(settings.ws_url), true))
        return false;
    for (size_t i = 0; i < std::strlen(settings.ssid); ++i) {
        const auto c = static_cast<unsigned char>(settings.ssid[i]);
        if (c < 0x20 || c == 0x7f)
            return false;
    }
    const size_t pass_len = std::strlen(settings.pass);
    if (pass_len != 0 && (pass_len < 8 || pass_len > 63))
        return false;
    for (size_t i = 0; i < pass_len; ++i) {
        const auto c = static_cast<unsigned char>(settings.pass[i]);
        if (c < 0x20 || c > 0x7e)
            return false;
    }
    return valid_wss_url(settings.ws_url);
}

bool parse(std::string_view body, Settings& output) {
    support::secure_erase(output);
    output.magic = kMagic;
    bool fields[3] { };
    bool ok = !body.empty();
    while (ok && !body.empty()) {
        auto split = body.find('&');
        auto token = body.substr(0, split);
        auto equal = token.find('=');
        if (equal == token.npos) {
            ok = false;
            break;
        }
        auto name = token.substr(0, equal);
        int index = name == "ssid" ? 0 : name == "pass" ? 1 : name == "ws_url" ? 2 : -1;
        if (index < 0 || fields[index]) {
            ok = false;
            break;
        }
        fields[index] = true;
        char* dest = index == 0 ? output.ssid : index == 1 ? output.pass : output.ws_url;
        size_t capacity = index == 0 ? sizeof(output.ssid) : index == 1 ? sizeof(output.pass) : sizeof(output.ws_url);
        if (!decode(token.substr(equal + 1), dest, capacity)) {
            ok = false;
            break;
        }
        if (split == body.npos)
            break;
        body.remove_prefix(split + 1);
        if (body.empty()) {
            ok = false;
            break;
        }
    }
    ok = ok && fields[0] && fields[1] && fields[2] && valid_settings(output);
    if (!ok)
        support::secure_erase(output);
    return ok;
}

} // namespace mosaic::features::provisioning::form
