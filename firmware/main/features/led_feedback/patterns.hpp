#pragma once

#include "../../drivers/leds_logic.hpp"
#include "../../events/status.hpp"

#include <cstdint>
#include <span>

namespace mosaic::features::led_feedback {

using drivers::leds::Color;

enum class Effect : uint8_t {
    off,
    solid,
    breathe, // Fades up and back down once per period.
    blink, // On for the first half of each period.
    spin, // One lit LED with a fading tail, one lap per period.
};

struct Animation {
    Effect effect = Effect::off;
    // Full scale; the driver applies the brightness ceiling.
    Color color;
    // One cycle of breathe, blink or spin. Ignored by off and solid.
    uint16_t period_ms = 0;
    bool operator==(const Animation&) const = default;
};

// What the ring and the strip show together.
struct Pattern {
    Animation ring;
    Animation strip;
    bool operator==(const Pattern&) const = default;
};

struct Notification {
    Pattern pattern;
    // How long it plays before the status pattern resumes.
    uint32_t duration_ms;
};

const Pattern& pattern(events::Status status);
const Notification& notification();

// Fills leds with one frame of the animation, elapsed_ms after it started.
void render(const Animation& animation, uint32_t elapsed_ms, std::span<Color> leds);

// Decides what the LEDs show: the status pattern, interrupted by notifications. Times are
// milliseconds from one monotonic clock and may wrap around.
class Feedback {
public:
    Feedback(events::Status status, uint32_t now_ms);
    void set_status(events::Status status, uint32_t now_ms);
    // Plays the notification pattern from the start, even if one is already playing.
    void notify(uint32_t now_ms);
    void render(uint32_t now_ms, std::span<Color> ring, std::span<Color> strip);

private:
    events::Status status_;
    uint32_t status_since_;
    bool notifying_ = false;
    uint32_t notified_at_ = 0;
};

}
