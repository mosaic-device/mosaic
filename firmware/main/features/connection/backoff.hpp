#pragma once

#include <cstdint>

namespace mosaic::features::connection {

// Reconnect timing from the wiki's Device Connection Protocol, section 6: the first
// retry waits about a second, each failure after that doubles the wait up to a minute,
// and every wait varies by up to 25% either way so devices that lost the same server
// don't all retry at once.
inline constexpr uint32_t kFirstDelayMs = 1'000;
inline constexpr uint32_t kMaxDelayMs = 60'000;

// The wait after `failures` failed attempts in a row, before jitter: 1 s, 2 s, 4 s...
// capped at kMaxDelayMs. Zero failures also gives kFirstDelayMs.
uint32_t nominal_delay_ms(uint32_t failures);

// `nominal` varied by -25% to just under +25%. `random` is any uniformly random 32-bit
// value; 0x8000'0000 gives `nominal` exactly. Waits at the cap can reach about 75 s.
uint32_t jittered_delay_ms(uint32_t nominal, uint32_t random);

// Counts failed attempts in a row and gives the wait before the next one.
class Backoff {
public:
    // Records one more failed attempt and returns the wait before the next. The caller
    // passes a random value (esp_random() on the board) so tests can fix it.
    uint32_t next_delay_ms(uint32_t random);
    // Call when the server replies with paired or pairing_pending: the next failure
    // starts again from kFirstDelayMs.
    void reset();
    // Failed attempts since the last reset. Stops counting at UINT32_MAX.
    uint32_t failures() const;

private:
    uint32_t failures_ = 0;
};

} // namespace mosaic::features::connection
