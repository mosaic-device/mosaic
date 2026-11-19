#include "backoff.hpp"

#include <limits>

namespace mosaic::features::connection {

uint32_t nominal_delay_ms(uint32_t failures) {
    uint32_t delay = kFirstDelayMs;
    // Stop doubling at the cap, so a long outage never overflows.
    for (uint32_t i = 1; i < failures && delay < kMaxDelayMs; ++i)
        delay *= 2;
    return delay < kMaxDelayMs ? delay : kMaxDelayMs;
}

uint32_t jittered_delay_ms(uint32_t nominal, uint32_t random) {
    // nominal * (0.75 + 0.5 * random / 2^32), in integers.
    const uint64_t spread = (static_cast<uint64_t>(nominal / 2) * random) >> 32;
    return nominal - nominal / 4 + static_cast<uint32_t>(spread);
}

uint32_t Backoff::next_delay_ms(uint32_t random) {
    if (failures_ < std::numeric_limits<uint32_t>::max())
        ++failures_;
    return jittered_delay_ms(nominal_delay_ms(failures_), random);
}

void Backoff::reset() {
    failures_ = 0;
}

uint32_t Backoff::failures() const {
    return failures_;
}

} // namespace mosaic::features::connection
