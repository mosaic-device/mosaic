#pragma once

#include <cstdint>

namespace mosaic::ui::navigation {

// Holds Home back for a moment after the device is paired, so the pairing screen can say so
// first. A device that starts paired goes straight to Home. Times are milliseconds from one
// clock and may wrap around.
class PairedHold {
public:
    static constexpr uint32_t kHoldMs = 3'000;

    // Whether to treat the device as paired yet. Call every tick with events::paired().
    bool paired(bool paired, uint32_t now_ms) {
        if (!started_) {
            started_ = true;
            was_paired_ = paired;
        }
        if (paired && !was_paired_) {
            holding_ = true;
            since_ms_ = now_ms;
        }
        was_paired_ = paired;
        if (holding_ && (!paired || now_ms - since_ms_ >= kHoldMs))
            holding_ = false;
        return paired && !holding_;
    }
    // While held: the pairing screen shows that pairing worked.
    bool holding() const {
        return holding_;
    }

private:
    bool started_ = false;
    bool was_paired_ = false;
    bool holding_ = false;
    uint32_t since_ms_ = 0;
};

} // namespace mosaic::ui::navigation
