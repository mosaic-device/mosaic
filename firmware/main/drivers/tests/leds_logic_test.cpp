#include "../leds_logic.hpp"

#include <cassert>

using namespace mosaic::drivers::leds;

static_assert(kCeiling == 76, "30% of 255");

int main() {
    // Every possible channel value stays at or below the ceiling, and the scale keeps order.
    uint8_t previous = 0;
    for (int value = 0; value <= 255; ++value) {
        const uint8_t out = limit(static_cast<uint8_t>(value));
        assert(out <= kCeiling);
        assert(out >= previous);
        previous = out;
    }
    assert(limit(uint8_t { 0 }) == 0);
    assert(limit(uint8_t { 255 }) == kCeiling);

    // Full white, the highest-current color, is capped on every channel.
    assert((limit(Color { 255, 255, 255 }) == Color { kCeiling, kCeiling, kCeiling }));
    assert((limit(Color { 255, 0, 128 }) == Color { kCeiling, 0, limit(uint8_t { 128 }) }));
}
