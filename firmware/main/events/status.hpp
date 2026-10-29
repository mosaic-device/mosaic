#pragma once

#include <cstdint>
#include <string_view>

namespace mosaic::events {

// The device's connection status. `error` holds until another status replaces it.
enum class Status : uint8_t { disconnected, pairing, connected, error };

const char* name(Status status);
// Accepts the names returned by name(); rejects anything else.
bool parse(std::string_view text, Status& status);

} // namespace mosaic::events
