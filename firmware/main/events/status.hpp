#pragma once

#include <cstdint>
#include <string_view>

namespace mosaic::events {

// The device's connection status. `error` holds until another status replaces it.
// `update_required`: the server doesn't speak this firmware's protocol version.
enum class Status : uint8_t { disconnected, pairing, connected, error, update_required };

// Whether the device is paired once status follows: connecting proves it, `disconnected`
// and `pairing` mean it is not, and an error or a required update leaves it as it was,
// since a paired device that cannot reach the server is still paired.
constexpr bool paired_after(bool paired, Status status) {
    switch (status) {
    case Status::connected:
        return true;
    case Status::disconnected:
    case Status::pairing:
        return false;
    case Status::error:
    case Status::update_required:
        break;
    }
    return paired;
}

const char* name(Status status);
// Accepts the names returned by name(); rejects anything else.
bool parse(std::string_view text, Status& status);

} // namespace mosaic::events
