#include "../patterns.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>

using namespace mosaic::features::led_feedback;
using mosaic::drivers::leds::kCeiling;
using mosaic::drivers::leds::limit;
using mosaic::events::Status;

namespace {
constexpr Color kOff { };
constexpr Color kRed { .red = 200, .green = 0, .blue = 0 };
constexpr std::array kStatuses { Status::disconnected, Status::pairing, Status::connected, Status::error };

// Matches the hardware: an 8-LED ring and a 12-LED strip.
using Ring = std::array<Color, 8>;
using Strip = std::array<Color, 12>;

bool all(std::span<const Color> leds, Color color) {
    return std::ranges::all_of(leds, [color](Color led) { return led == color; });
}

bool dark(std::span<const Color> leds) {
    return all(leds, kOff);
}

int distance(Color a, Color b) {
    return std::abs(a.red - b.red) + std::abs(a.green - b.green) + std::abs(a.blue - b.blue);
}

// Long enough for any fade to finish.
constexpr uint32_t kSettled = kFadeMs + 1;

void off_and_solid() {
    Ring leds;
    leds.fill(kRed);
    render({ .effect = Effect::off, .color = kRed }, 123, leds);
    assert(dark(leds));
    render({ .effect = Effect::solid, .color = kRed }, 123, leds);
    assert(all(leds, kRed));
}

void breathe_fades_between_dim_and_full_without_going_dark() {
    const Animation animation { .effect = Effect::breathe, .color = kRed, .period_ms = 1000 };
    Ring leds;
    uint8_t previous = 0;
    for (uint32_t t = 0; t <= 500; t += 10) {
        render(animation, t, leds);
        assert(leds[0].red >= previous);
        assert(all(leds, leds[0]));
        previous = leds[0].red;
    }
    render(animation, 0, leds);
    assert(!dark(leds) && leds[0].red < kRed.red / 2);
    render(animation, 500, leds);
    assert(all(leds, kRed));
    for (uint32_t t = 500; t <= 1000; t += 10) {
        render(animation, t, leds);
        assert(leds[0].red <= previous);
        previous = leds[0].red;
    }
    // The fade is symmetric and repeats every period.
    Ring rising, falling;
    render(animation, 200, rising);
    render(animation, 800, falling);
    assert(rising == falling);
    render(animation, 3200, falling);
    assert(rising == falling);
    // A missing period holds the color steady instead of dividing by zero.
    render({ .effect = Effect::breathe, .color = kRed, .period_ms = 0 }, 123, leds);
    assert(all(leds, kRed));
}

void each_status_means_one_thing() {
    // Nothing paired yet: the device is being set up, and the screen explains it.
    assert(pattern(Status::disconnected, false) == Pattern { });
    // Pairing asks for the owner at the knob; the room light stays off until paired.
    assert(pattern(Status::pairing, false).ring.effect == Effect::breathe);
    assert(pattern(Status::pairing, false).strip.effect == Effect::off);
    // Idle and paired: only the ambient strip is on.
    const Pattern idle = pattern(Status::connected, true);
    assert(idle.ring.effect == Effect::off && idle.strip.effect == Effect::solid);
    // Losing the server lights the ring steadily, never blinking, and keeps the room
    // light on if the device was paired.
    const Pattern trouble = pattern(Status::error, true);
    assert(trouble.ring.effect == Effect::solid);
    assert(trouble.strip == idle.strip);
    assert(pattern(Status::error, false).ring == trouble.ring);
    assert(pattern(Status::error, false).strip.effect == Effect::off);
    // Trouble, pairing and a notification each use their own ring color.
    const std::array colors {
        trouble.ring.color,
        pattern(Status::pairing, false).ring.color,
        kNotificationRing.color,
    };
    for (size_t i = 0; i < colors.size(); ++i)
        for (size_t j = i + 1; j < colors.size(); ++j)
            assert(distance(colors[i], colors[j]) > 150);
}

// The driver sends every frame through limit(); check the result for every pattern over time.
void no_pattern_exceeds_the_ceiling() {
    Ring ring;
    Strip strip;
    for (Status status : kStatuses) {
        for (bool paired : { false, true }) {
            Feedback feedback(status, paired);
            feedback.notify(500);
            for (uint32_t t = 0; t < 10'000; t += 7) {
                feedback.render(t, ring, strip);
                for (std::span<const Color> leds : { std::span<const Color>(ring), std::span<const Color>(strip) }) {
                    for (Color led : leds) {
                        const Color sent = limit(led);
                        assert(sent.red <= kCeiling && sent.green <= kCeiling && sent.blue <= kCeiling);
                    }
                }
            }
        }
    }
}

void changes_crossfade_without_jumping() {
    Feedback feedback(Status::disconnected, false);
    Ring ring;
    Strip strip;
    feedback.render(0, ring, strip);
    assert(dark(ring) && dark(strip));

    // Connecting fades the strip up from dark over kFadeMs, a little at a time.
    feedback.set_status(Status::connected, true);
    feedback.render(1000, ring, strip);
    assert(dark(strip));
    Strip previous = strip;
    for (uint32_t t = 1000; t <= 1000 + kFadeMs; t += 20) {
        feedback.render(t, ring, strip);
        assert(distance(strip[0], previous[0]) < 60);
        assert(strip[0].red >= previous[0].red);
        previous = strip;
    }
    Strip settled;
    render(pattern(Status::connected, true).strip, kSettled, settled);
    feedback.render(1000 + kSettled, ring, strip);
    assert(strip == settled && dark(ring));

    // A change in the middle of a fade starts from what is showing, not from the old target.
    feedback.set_status(Status::error, true);
    feedback.render(5000, ring, strip);
    feedback.render(5000 + kFadeMs / 2, ring, strip);
    const Color halfway = ring[0];
    assert(halfway != kOff && halfway != pattern(Status::error, true).ring.color);
    feedback.set_status(Status::disconnected, false);
    feedback.render(5000 + kFadeMs / 2 + 1, ring, strip);
    assert(distance(ring[0], halfway) < 20);
    feedback.render(5000 + kFadeMs / 2 + kSettled, ring, strip);
    assert(dark(ring) && dark(strip));

    // Setting the same status again changes nothing.
    feedback.set_status(Status::connected, true);
    feedback.render(9000 + kSettled, ring, strip);
    const Strip steady = strip;
    feedback.set_status(Status::connected, true);
    feedback.render(9000 + kSettled + 1, ring, strip);
    assert(strip == steady);
}

void a_notification_waits_on_the_ring_until_acknowledged() {
    Feedback feedback(Status::connected, true);
    Ring ring;
    Strip strip;
    feedback.render(0, ring, strip);
    feedback.render(kSettled, ring, strip);
    const Strip idle = strip;

    feedback.notify(1000);
    assert(feedback.waiting());
    feedback.render(1000, ring, strip);
    feedback.render(1000 + kSettled, ring, strip);
    assert(all(ring, kNotificationColor));
    // Long after the strip's swell, the ring still holds the color.
    feedback.render(600'000, ring, strip);
    assert(all(ring, kNotificationColor));
    assert(strip == idle);

    feedback.acknowledge();
    assert(!feedback.waiting());
    feedback.render(600'000, ring, strip);
    feedback.render(600'000 + kSettled, ring, strip);
    assert(dark(ring));

    // While the server is out of reach, a waiting notification still wins the ring,
    // and acknowledging it shows the trouble color again.
    feedback.set_status(Status::error, true);
    feedback.notify(700'000);
    feedback.render(700'000, ring, strip);
    feedback.render(700'000 + kSettled, ring, strip);
    assert(all(ring, kNotificationColor));
    feedback.acknowledge();
    feedback.render(700'000 + kSettled, ring, strip);
    feedback.render(700'000 + 2 * kSettled, ring, strip);
    assert(all(ring, pattern(Status::error, true).ring.color));

    // Unpairing clears it.
    feedback.notify(800'000);
    feedback.set_status(Status::disconnected, false);
    assert(!feedback.waiting());
    feedback.render(800'001, ring, strip);
    feedback.render(800'001 + kSwellMs + kSettled, ring, strip);
    assert(dark(ring) && dark(strip));
}

void the_strip_swells_once_then_settles() {
    Feedback feedback(Status::connected, true);
    Ring ring;
    Strip strip;
    feedback.render(0, ring, strip);
    feedback.render(kSettled, ring, strip);
    const Strip idle = strip;

    feedback.notify(1000);
    feedback.render(1000, ring, strip);
    assert(strip == idle);
    // At the peak the strip shows the notification color.
    feedback.render(1000 + kSwellMs / 2, ring, strip);
    assert(all(strip, kNotificationColor));
    // It never jumps along the way.
    Strip previous = idle;
    for (uint32_t t = 1000; t <= 1000 + kSwellMs; t += 20) {
        feedback.render(t, ring, strip);
        assert(distance(strip[0], previous[0]) < 60);
        previous = strip;
    }
    feedback.render(1000 + kSwellMs, ring, strip);
    assert(strip == idle);

    // Another notification while it falls rises again from where it is.
    feedback.notify(10'000);
    feedback.render(10'000 + kSwellMs - 300, ring, strip);
    const Color falling = strip[0];
    feedback.notify(10'000 + kSwellMs - 300);
    feedback.render(10'000 + kSwellMs - 299, ring, strip);
    assert(distance(strip[0], falling) < 20);
    feedback.render(10'000 + kSwellMs + 1000, ring, strip);
    assert(all(strip, kNotificationColor));
}

void times_may_wrap_around() {
    const uint32_t start = UINT32_MAX - 50;
    Feedback feedback(Status::connected, true);
    Ring ring;
    Strip strip;
    feedback.render(start, ring, strip);
    feedback.notify(start);
    feedback.render(start + kSwellMs / 2, ring, strip); // Wrapped past zero.
    assert(all(strip, kNotificationColor));
    feedback.render(start + kSwellMs + kSettled, ring, strip);
    Strip idle;
    render(pattern(Status::connected, true).strip, 0, idle);
    assert(strip == idle && all(ring, kNotificationColor));
}
} // namespace

int main() {
    off_and_solid();
    breathe_fades_between_dim_and_full_without_going_dark();
    each_status_means_one_thing();
    no_pattern_exceeds_the_ceiling();
    changes_crossfade_without_jumping();
    a_notification_waits_on_the_ring_until_acknowledged();
    the_strip_swells_once_then_settles();
    times_may_wrap_around();
}
