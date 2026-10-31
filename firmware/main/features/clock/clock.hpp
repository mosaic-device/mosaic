#pragma once

#include <cstddef>
#include <ctime>
#include <span>
#include <string_view>

namespace mosaic::features::clock {

// Arizona, as a POSIX TZ string: UTC-7 all year, with no daylight saving. The device
// is developed there; until the owner's zone comes from setup or the server, every
// device shows this zone.
inline constexpr char kTimeZone[] = "MST7";

// Shown until the time is synced over the network this boot, and if sync never works.
inline constexpr std::string_view kPlaceholder = "--:--";

// Times before this (2026-01-01 UTC) come from a clock that was never set.
inline constexpr std::time_t kEarliestValid = 1'767'225'600;

// Room for the longest clock text, "12:00", and its terminator.
inline constexpr size_t kTextSize = 6;

// The Home clock's text: 12-hour local time without AM/PM, such as "2:05". The
// placeholder unless the time was synced this boot and is plausible, so a clock kept
// across a restart without Wi-Fi is never shown as correct. Converts with the
// process time zone (TZ); the clock service sets it to kTimeZone.
std::string_view text(bool synced, std::time_t now, std::span<char, kTextSize> buffer);

}
