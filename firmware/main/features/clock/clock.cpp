#include "clock.hpp"

#include <cstdio>

namespace mosaic::features::clock {
namespace {
    // Spelled out here rather than by strftime, so the text never depends on a C locale.
    constexpr const char* kWeekdays[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
        "Saturday" };
    constexpr const char* kMonths[] = { "January", "February", "March", "April", "May", "June", "July", "August",
        "September", "October", "November", "December" };

    bool trusted(bool synced, std::time_t now, std::tm& local) {
        return synced && now >= kEarliestValid && localtime_r(&now, &local);
    }
} // namespace

std::string_view text(bool synced, std::time_t now, std::span<char, kTextSize> buffer) {
    std::tm local { };
    if (!trusted(synced, now, local))
        return kPlaceholder;
    const int hour = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
    const int length = std::snprintf(buffer.data(), buffer.size(), "%d:%02d", hour, local.tm_min);
    if (length <= 0 || static_cast<size_t>(length) >= buffer.size())
        return kPlaceholder;
    return { buffer.data(), static_cast<size_t>(length) };
}

std::string_view date(bool synced, std::time_t now, std::span<char, kDateSize> buffer) {
    std::tm local { };
    if (!trusted(synced, now, local) || local.tm_wday < 0 || local.tm_wday > 6 || local.tm_mon < 0 || local.tm_mon > 11)
        return { };
    const int length = std::snprintf(
        buffer.data(), buffer.size(), "%s, %s %d", kWeekdays[local.tm_wday], kMonths[local.tm_mon], local.tm_mday);
    if (length <= 0 || static_cast<size_t>(length) >= buffer.size())
        return { };
    return { buffer.data(), static_cast<size_t>(length) };
}

}
