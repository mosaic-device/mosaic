#pragma once

#include <cstdint>
#include <string_view>

namespace mosaic::features::provisioning::form {

// Stored as one versioned NVS blob; do not change this layout without migration.
inline constexpr uint32_t kMagic = 0x4d4f5331;
struct Settings {
    uint32_t magic = kMagic;
    char ssid[33] { };
    char pass[65] { };
    char ws_url[193] { };
};

// Percent encoding can use three bytes per value byte, plus field names/separators.
inline constexpr size_t kEncodedBodyLimit = sizeof("ssid=&pass=&ws_url=") - 1
    + 3 * (sizeof(Settings::ssid) - 1 + sizeof(Settings::pass) - 1 + sizeof(Settings::ws_url) - 1);

bool valid_wss_url(std::string_view url);
bool valid_settings(const Settings& settings);
// On failure the output is zeroed (no partially decoded password remains).
bool parse(std::string_view body, Settings& output);

} // namespace mosaic::features::provisioning::form
