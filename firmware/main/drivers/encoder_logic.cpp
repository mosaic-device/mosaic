#include "encoder_logic.hpp"

namespace mosaic::drivers::encoder {
int32_t DetentCounter::update(int32_t raw_count, bool at_rest) {
    // Wrap-safe difference, so a counter that wraps still yields the short distance.
    const int32_t delta = static_cast<int32_t>(static_cast<uint32_t>(raw_count) - static_cast<uint32_t>(origin_));
    // Truncating relative to the last boundary keeps both directions symmetric:
    // chatter of up to three edges either way never emits a step.
    int32_t steps = delta / kEdgesPerDetent;
    origin_ = static_cast<int32_t>(static_cast<uint32_t>(origin_) + static_cast<uint32_t>(steps * kEdgesPerDetent));
    if (at_rest) {
        const int32_t remainder = delta - steps * kEdgesPerDetent;
        // Three edges into a detent while resting means one edge was lost: count the click.
        // Smaller offsets are spurious edges; drop them. Either way, the rest position is the new boundary.
        if (remainder > kEdgesPerDetent / 2)
            ++steps;
        else if (remainder < -kEdgesPerDetent / 2)
            --steps;
        origin_ = raw_count;
    }
    return steps;
}

bool PressLatch::update(ButtonEvents events) {
    // Consume one event per read, alternating, so every press gets a pressed read
    // and a released read. A press still held keeps reporting pressed.
    if (!reported_ && events.downs != seen_.downs) {
        ++seen_.downs;
        reported_ = true;
    } else if (reported_ && events.ups != seen_.ups) {
        ++seen_.ups;
        reported_ = false;
    }
    return reported_;
}

}
