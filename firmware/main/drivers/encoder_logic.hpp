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

// What a debounced press means. A press held past the long-press time is Back and
// never reaches LVGL, so a long press cannot also click or toggle a slider's edit mode.
enum class Press : uint8_t { none, click, back };

// Sorts each press into a click or Back. iot_button reports both events from one task.
class PressSorter {
public:
    // The press has been held for the long-press time: Back, once per press.
    Press long_press_started();
    // The press ended: a click, unless it already became Back.
    Press released();

private:
    bool held_ = false;
};

// Turns the running click count into the state LVGL reads. Each click is reported
// as pressed on one read and released on the next, so clicks between two reads are
// replayed one at a time, never merged.
class PressLatch {
public:
    bool update(uint32_t clicks);

private:
    bool reported_ = false;
    uint32_t seen_ = 0;
};

}
