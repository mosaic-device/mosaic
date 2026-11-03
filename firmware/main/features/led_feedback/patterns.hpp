#pragma once

#include "../../drivers/leds_logic.hpp"
#include "../../events/status.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace mosaic::features::led_feedback {

using drivers::leds::Color;

// What the lights mean. The ring, around the knob, lights only when something wants
// the owner: a notification waiting, pairing in progress, or the server out of reach.
// The strip is ambient light, on whenever the device is paired. Nothing blinks or
// spins; every change crossfades.
enum class Effect : uint8_t {
    off,
    solid,
    breathe, // Fades between a quarter and full brightness, once per period.
};

struct Animation {
    Effect effect = Effect::off;
    // Full scale; the driver applies the brightness ceiling and the room's level.
    Color color;
    // One cycle of breathe. Ignored by off and solid.
    uint16_t period_ms = 0;
    bool operator==(const Animation&) const = default;
};

// What the ring and the strip show together.
struct Pattern {
    Animation ring;
    Animation strip;
    bool operator==(const Pattern&) const = default;
};

// The lights for a status, before any notification. paired is events::paired_after()
// of the statuses so far: a paired device that loses the server keeps its strip on.
Pattern pattern(events::Status status, bool paired);

// A waiting notification: the ring holds this color until the owner interacts with the
// device, and the strip swells to it once.
inline constexpr Color kNotificationColor { .red = 30, .green = 100, .blue = 255 };
inline constexpr Animation kNotificationRing { .effect = Effect::solid, .color = kNotificationColor };
// The strip's swell: up, hold, then back to its pattern, over this long.
inline constexpr uint32_t kSwellMs = 3000;
// Every change of pattern crossfades over this long.
inline constexpr uint32_t kFadeMs = 800;

// Fills leds with one frame of the animation, elapsed_ms after it started.
void render(const Animation& animation, uint32_t elapsed_ms, std::span<Color> leds);

// Decides what the LEDs show from the status and notifications. Times are milliseconds
// from one monotonic clock and may wrap around.
class Feedback {
public:
    // The most LEDs either output may have.
    static constexpr size_t kMaxLeds = 32;

    // paired is events::paired() for the same status.
    Feedback(events::Status status, bool paired);
    // Shows from the next render(), fading from what is showing then. Becoming unpaired
    // also clears a waiting notification.
    void set_status(events::Status status, bool paired);
    // Turns the ring to the notification color until acknowledge(), and swells the strip.
    void notify(uint32_t now_ms);
    // The owner touched, turned or pressed something: the notification has been seen.
    void acknowledge();
    bool waiting() const {
        return waiting_;
    }
    void render(uint32_t now_ms, std::span<Color> ring, std::span<Color> strip);

private:
    // One output's crossfade from whatever it last showed to its current animation.
    struct Light {
        Animation target;
        uint32_t since = 0;
        bool started = false;
        std::array<Color, kMaxLeds> from { };
        std::array<Color, kMaxLeds> shown { };
        void render(const Animation& animation, uint32_t now_ms, std::span<Color> leds);
    };

    events::Status status_;
    bool paired_;
    bool waiting_ = false;
    bool swelling_ = false;
    uint32_t notified_at_ = 0;
    Light ring_;
    Light strip_;
};

}
