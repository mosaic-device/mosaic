#include "../clock.hpp"

#include <array>
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <string_view>

using namespace mosaic::features::clock;

namespace {
// 2026-07-01T00:00:00Z and 2026-01-15T00:00:00Z: summer and winter.
constexpr std::time_t kJuly = 1'782'864'000;
constexpr std::time_t kJanuary = 1'768'435'200;
constexpr std::time_t kHour = 3600;
constexpr std::time_t kMinute = 60;

std::string_view shown(bool synced, std::time_t now) {
    static std::array<char, kTextSize> buffer;
    return text(synced, now, buffer);
}

void shows_the_placeholder_until_synced() {
    // Before sync, even a plausible time (kept across a restart) is not shown.
    assert(shown(false, kJuly) == kPlaceholder);
    // A clock never set reads near 1970; it is not trusted even if marked synced.
    assert(shown(true, 0) == kPlaceholder);
    assert(shown(true, kEarliestValid - 1) == kPlaceholder);
    assert(shown(true, kEarliestValid) != kPlaceholder);
}

void twelve_hour_without_am_pm() {
    // Arizona is UTC-7: 07:00Z is midnight.
    const std::time_t midnight = kJuly + 7 * kHour;
    assert(shown(true, midnight) == "12:00");
    assert(shown(true, midnight + 9 * kHour + 5 * kMinute) == "9:05");
    assert(shown(true, midnight + 12 * kHour + 30 * kMinute) == "12:30");
    assert(shown(true, midnight + 13 * kHour + 7 * kMinute) == "1:07");
    assert(shown(true, midnight + 23 * kHour + 59 * kMinute) == "11:59");
    // Seconds never show; the minute changes on the minute.
    assert(shown(true, midnight + 59) == "12:00");
    assert(shown(true, midnight + 60) == "12:01");
}

void arizona_has_no_daylight_saving() {
    // 19:05Z is 12:05 local in both summer and winter.
    assert(shown(true, kJuly + 19 * kHour + 5 * kMinute) == "12:05");
    assert(shown(true, kJanuary + 19 * kHour + 5 * kMinute) == "12:05");
    // Early UTC is still the previous local evening.
    assert(shown(true, kJanuary + 6 * kHour + 59 * kMinute) == "11:59");
}

void fits_the_buffer() {
    std::array<char, kTextSize> buffer;
    const std::string_view longest = text(true, kJuly + 7 * kHour + 10 * kHour + 59 * kMinute, buffer);
    assert(longest == "10:59" && longest.size() < kTextSize);
}
void the_date_follows_the_local_day() {
    std::array<char, kDateSize> buffer;
    // 2026-07-01 was a Wednesday; 06:59Z is still June 30 in Arizona.
    assert(date(true, kJuly + 7 * kHour, buffer) == "Wednesday, July 1");
    assert(date(true, kJuly + 6 * kHour + 59 * kMinute, buffer) == "Tuesday, June 30");
    assert(date(true, kJanuary + 19 * kHour, buffer) == "Thursday, January 15");
    // No date while the time is not trusted.
    assert(date(false, kJuly, buffer).empty());
    assert(date(true, kEarliestValid - 1, buffer).empty());
    // The longest weekday with the longest month and a two-digit day fits.
    const std::time_t september_30 = 1'790'726'400 + 19 * kHour; // noon on 2026-09-30, a Wednesday.
    const std::string_view longest = date(true, september_30, buffer);
    assert(longest == "Wednesday, September 30" && longest.size() < kDateSize);
}
} // namespace

int main() {
    // The service sets this zone on the device; the host uses the same rules.
    setenv("TZ", kTimeZone, 1);
    tzset();
    shows_the_placeholder_until_synced();
    twelve_hour_without_am_pm();
    arizona_has_no_daylight_saving();
    fits_the_buffer();
    the_date_follows_the_local_day();
}
