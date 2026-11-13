#include "../backoff.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>

using namespace mosaic::features::connection;

namespace {
// Random values that give the shortest, exact and longest waits.
constexpr uint32_t kLowest = 0;
constexpr uint32_t kMiddle = 0x8000'0000;
constexpr uint32_t kHighest = std::numeric_limits<uint32_t>::max();

void doubles_from_one_second_to_the_cap() {
    assert(nominal_delay_ms(0) == 1'000);
    assert(nominal_delay_ms(1) == 1'000);
    assert(nominal_delay_ms(2) == 2'000);
    assert(nominal_delay_ms(3) == 4'000);
    assert(nominal_delay_ms(6) == 32'000);
    // 64 s would pass the cap.
    assert(nominal_delay_ms(7) == kMaxDelayMs);
    assert(nominal_delay_ms(1'000) == kMaxDelayMs);
    assert(nominal_delay_ms(std::numeric_limits<uint32_t>::max()) == kMaxDelayMs);
}

void jitter_stays_within_a_quarter() {
    assert(jittered_delay_ms(1'000, kMiddle) == 1'000);
    assert(jittered_delay_ms(1'000, kLowest) == 750);
    // Just under +25%, never over it.
    assert(jittered_delay_ms(1'000, kHighest) == 1'249);
    assert(jittered_delay_ms(kMaxDelayMs, kLowest) == 45'000);
    assert(jittered_delay_ms(kMaxDelayMs, kHighest) == 74'999);
    for (uint32_t random = 0; random < 0xF000'0000; random += 0x0100'0001) {
        const uint32_t delay = jittered_delay_ms(4'000, random);
        assert(delay >= 3'000 && delay < 5'000);
    }
}

void counts_failures_and_resets_after_a_reply() {
    Backoff backoff;
    assert(backoff.failures() == 0);
    assert(backoff.next_delay_ms(kMiddle) == 1'000);
    assert(backoff.next_delay_ms(kMiddle) == 2'000);
    assert(backoff.next_delay_ms(kMiddle) == 4'000);
    assert(backoff.failures() == 3);
    // The server replied: the next failure waits a second again.
    backoff.reset();
    assert(backoff.failures() == 0);
    assert(backoff.next_delay_ms(kMiddle) == 1'000);
}

void a_long_outage_stays_at_the_cap() {
    Backoff backoff;
    uint32_t delay = 0;
    for (int i = 0; i < 100; ++i)
        delay = backoff.next_delay_ms(kMiddle);
    assert(delay == kMaxDelayMs);
    assert(backoff.failures() == 100);
}
} // namespace

int main() {
    doubles_from_one_second_to_the_cap();
    jitter_stays_within_a_quarter();
    counts_failures_and_resets_after_a_reply();
    a_long_outage_stays_at_the_cap();
    std::puts("Backoff tests passed");
}
