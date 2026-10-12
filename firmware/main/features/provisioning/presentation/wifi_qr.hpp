#pragma once

#include <cstddef>
#include <string_view>

namespace mosaic::features::provisioning {

// Wi-Fi QR payload. Escaping keeps the encoding valid if the AP name or
// password alphabet changes later. The caller owns and clears the buffer.
inline bool wifi_join_payload(std::string_view ssid, std::string_view password, char* out, size_t capacity) {
    if (!out || !capacity || ssid.empty() || password.empty())
        return false;

    size_t size = 0;
    auto append = [&](char ch) {
        if (size + 1 >= capacity)
            return false;
        out[size++] = ch;
        return true;
    };
    auto literal = [&](std::string_view value) {
        for (char ch : value) {
            if (!append(ch))
                return false;
        }
        return true;
    };
    auto escaped = [&](std::string_view value) {
        for (char ch : value) {
            if (ch == '\\' || ch == ';' || ch == ',' || ch == ':') {
                if (!append('\\'))
                    return false;
            }
            if (!append(ch))
                return false;
        }
        return true;
    };

    const bool valid
        = literal("WIFI:T:WPA;S:") && escaped(ssid) && literal(";P:") && escaped(password) && literal(";;");
    if (!valid) {
        volatile char* bytes = out;
        for (size_t i = 0; i < size; ++i)
            bytes[i] = 0;
        out[0] = '\0';
        return false;
    }
    out[size] = '\0';
    return true;
}

} // namespace mosaic::features::provisioning
