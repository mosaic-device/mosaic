#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace mosaic::events {

// The pairing code the server issued, as the device shows it. Anyone holding a live code can
// claim the device: never log it.
struct PairingCode {
    // Eight characters from the server, without the dash.
    static constexpr size_t kLength = 8;
    std::array<char, kLength + 1> text { };
    // Monotonic milliseconds (esp_timer) when the code stops working. Zero: no code.
    uint64_t expires_at_ms = 0;

    bool empty() const {
        return text[0] == '\0';
    }
    std::string_view view() const {
        return text.data();
    }
    bool operator==(const PairingCode&) const = default;
};

// A code that expires `expires_in_s` after `now_ms`. Anything but exactly kLength characters
// gives an empty code, which the screen shows as no code yet.
constexpr PairingCode make_pairing_code(std::string_view text, uint32_t expires_in_s, uint64_t now_ms) {
    PairingCode code;
    if (text.size() != PairingCode::kLength)
        return code;
    for (size_t i = 0; i < text.size(); ++i)
        code.text[i] = text[i];
    code.expires_at_ms = now_ms + uint64_t { expires_in_s } * 1'000;
    return code;
}

} // namespace mosaic::events
