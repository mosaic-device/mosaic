#pragma once

#include <cstdint>

namespace mosaic::drivers::encoder {

// The PCNT unit decodes quadrature in hardware and counts every A/B edge.
// A KY-040 detent is one full quadrature cycle.
constexpr int32_t kEdgesPerDetent = 4;

// Converts the running PCNT edge count into whole detent steps.
class DetentCounter {
public:
    // origin is the raw count at a detent, normally the cleared counter's 0.
    explicit DetentCounter(int32_t origin = 0)
        : origin_(origin) { }
    // at_rest means both phases read their detent level. When that holds, the
    // count is snapped to the nearest detent, so a missed or extra edge never
    // leaves later steps landing between clicks.
    int32_t update(int32_t raw_count, bool at_rest);

private:
    int32_t origin_; // Raw count at the last emitted detent boundary.
};

// Running totals of debounced button events. Named fields keep the two counts
// from being swapped at the call site.
struct ButtonEvents {
    uint32_t downs = 0;
    uint32_t ups = 0;
};

// Turns debounced press/release counts into the state LVGL reads. Each press is
// reported as pressed on one read and released on a later one, so a tap that
// starts and ends between two reads still registers as a click.
class PressLatch {
public:
    bool update(ButtonEvents events);

private:
    bool reported_ = false;
    ButtonEvents seen_;
};

}
