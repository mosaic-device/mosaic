#include "../patterns.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
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

void check_animation_is_defined(const Animation& animation) {
    if (animation.effect != Effect::off && animation.effect != Effect::solid)
        assert(animation.period_ms > 0);
}

void check_pattern_is_defined(const Pattern& pattern) {
    check_animation_is_defined(pattern.ring);
    check_animation_is_defined(pattern.strip);
}

void off_and_solid() {
    Ring leds;
    leds.fill(kRed);
    render({ .effect = Effect::off, .color = kRed }, 123, leds);
    assert(all(leds, kOff));
    render({ .effect = Effect::solid, .color = kRed }, 123, leds);
    assert(all(leds, kRed));
}

void breathe_fades_up_then_down() {
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
    assert(all(leds, kOff));
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
}

void blink_is_on_for_half_of_each_period() {
    const Animation animation { .effect = Effect::blink, .color = kRed, .period_ms = 1000 };
    Ring leds;
    for (uint32_t t : { 0u, 499u, 1000u, 1499u }) {
        render(animation, t, leds);
        assert(all(leds, kRed));
    }
    for (uint32_t t : { 500u, 999u, 1500u }) {
        render(animation, t, leds);
        assert(all(leds, kOff));
    }
}

void spin_moves_one_bright_led_around_every_position() {
    const Animation animation { .effect = Effect::spin, .color = kRed, .period_ms = 800 };
    Ring leds;
    std::array<bool, 8> visited { };
    for (uint32_t t = 0; t < 800; t += 10) {
        render(animation, t, leds);
        const auto brightest = std::ranges::count(leds, kRed);
        assert(brightest == 1);
        const size_t head = static_cast<size_t>(std::ranges::find(leds, kRed) - leds.begin());
        visited[head] = true;
        // The LED just behind the head is dimmer but lit; the one ahead is dark.
        const Color behind = leds[(head + 7) % 8];
        assert(behind != kOff && behind.red < kRed.red);
        assert(leds[(head + 1) % 8] == kOff);
    }
    assert(std::ranges::all_of(visited, [](bool seen) { return seen; }));
    // Works on any length, including the strip.
    Strip strip;
    render(animation, 400, strip);
    assert(std::ranges::count(strip, kRed) == 1);
}

void animated_effects_without_a_period_hold_steady() {
    Ring leds;
    for (Effect effect : { Effect::breathe, Effect::blink, Effect::spin }) {
        render({ .effect = effect, .color = kRed, .period_ms = 0 }, 123, leds);
        assert(all(leds, kRed));
    }
}

void every_pattern_is_defined_and_distinct() {
    for (Status status : kStatuses)
        check_pattern_is_defined(pattern(status));
    check_pattern_is_defined(notification().pattern);
    assert(notification().duration_ms > 0);
    // Idle, pairing, error and a new notification must each look different.
    const std::array distinct {
        pattern(Status::connected),
        pattern(Status::pairing),
        pattern(Status::error),
        notification().pattern,
    };
    for (size_t i = 0; i < distinct.size(); ++i)
        for (size_t j = i + 1; j < distinct.size(); ++j)
            assert(distinct[i] != distinct[j]);
}

// The driver sends every frame through limit(); check the result for every pattern over time.
void no_pattern_exceeds_the_ceiling() {
    std::array<Pattern, kStatuses.size() + 1> patterns;
    for (size_t i = 0; i < kStatuses.size(); ++i)
        patterns[i] = pattern(kStatuses[i]);
    patterns.back() = notification().pattern;
    Ring ring;
    Strip strip;
    for (const Pattern& shown : patterns) {
        for (uint32_t t = 0; t < 10'000; t += 7) {
            render(shown.ring, t, ring);
            render(shown.strip, t, strip);
            for (std::span<const Color> leds : { std::span<const Color>(ring), std::span<const Color>(strip) }) {
                for (Color led : leds) {
                    const Color sent = limit(led);
                    assert(sent.red <= kCeiling && sent.green <= kCeiling && sent.blue <= kCeiling);
                }
            }
        }
    }
}

void feedback_shows_the_status_pattern() {
    Feedback feedback(Status::pairing, 1000);
    Ring ring, expected_ring;
    Strip strip, expected_strip;
    feedback.render(1300, ring, strip);
    render(pattern(Status::pairing).ring, 300, expected_ring);
    render(pattern(Status::pairing).strip, 300, expected_strip);
    assert(ring == expected_ring && strip == expected_strip);

    // A new status starts its animation from the beginning.
    feedback.set_status(Status::error, 2000);
    feedback.render(2100, ring, strip);
    render(pattern(Status::error).ring, 100, expected_ring);
    render(pattern(Status::error).strip, 100, expected_strip);
    assert(ring == expected_ring && strip == expected_strip);

    // Setting the same status again does not restart it.
    feedback.set_status(Status::error, 2050);
    feedback.render(2100, ring, strip);
    assert(ring == expected_ring && strip == expected_strip);
}

void notification_interrupts_then_returns_to_the_status() {
    const uint32_t duration = notification().duration_ms;
    Feedback feedback(Status::connected, 0);
    Ring ring, expected_ring;
    Strip strip, expected_strip;

    feedback.notify(1000);
    feedback.render(1100, ring, strip);
    render(notification().pattern.ring, 100, expected_ring);
    render(notification().pattern.strip, 100, expected_strip);
    assert(ring == expected_ring && strip == expected_strip);

    // A status change during a notification shows once the notification ends.
    feedback.set_status(Status::error, 1200);
    feedback.render(1000 + duration - 1, ring, strip);
    render(notification().pattern.ring, duration - 1, expected_ring);
    assert(ring == expected_ring);
    feedback.render(1000 + duration, ring, strip);
    render(pattern(Status::error).ring, 1000 + duration - 1200, expected_ring);
    render(pattern(Status::error).strip, 1000 + duration - 1200, expected_strip);
    assert(ring == expected_ring && strip == expected_strip);

    // Another notification while one plays restarts it.
    feedback.notify(10'000);
    feedback.notify(10'000 + duration - 10);
    feedback.render(10'000 + duration + 10, ring, strip);
    render(notification().pattern.ring, 20, expected_ring);
    assert(ring == expected_ring);
}

void times_may_wrap_around() {
    const uint32_t start = UINT32_MAX - 50;
    Feedback feedback(Status::connected, 0);
    feedback.notify(start);
    Ring ring, expected;
    Strip strip;
    feedback.render(start + 100, ring, strip); // Wrapped past zero.
    render(notification().pattern.ring, 100, expected);
    assert(ring == expected);
    feedback.render(start + notification().duration_ms, ring, strip);
    render(pattern(Status::connected).ring, start + notification().duration_ms, expected);
    assert(ring == expected);
}
} // namespace

int main() {
    off_and_solid();
    breathe_fades_up_then_down();
    blink_is_on_for_half_of_each_period();
    spin_moves_one_bright_led_around_every_position();
    animated_effects_without_a_period_hold_steady();
    every_pattern_is_defined_and_distinct();
    no_pattern_exceeds_the_ceiling();
    feedback_shows_the_status_pattern();
    notification_interrupts_then_returns_to_the_status();
    times_may_wrap_around();
}
