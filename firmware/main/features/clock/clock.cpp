#include "clock.hpp"

#include <cstdio>

namespace mosaic::features::clock {

std::string_view text(bool synced, std::time_t now, std::span<char, kTextSize> buffer) {
    std::tm local { };
    if (!synced || now < kEarliestValid || !localtime_r(&now, &local))
        return kPlaceholder;
    const int hour = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
    const int length = std::snprintf(buffer.data(), buffer.size(), "%d:%02d", hour, local.tm_min);
    if (length <= 0 || static_cast<size_t>(length) >= buffer.size())
        return kPlaceholder;
    return { buffer.data(), static_cast<size_t>(length) };
}

}
