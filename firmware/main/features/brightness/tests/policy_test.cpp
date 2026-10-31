#include "../policy.hpp"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <utility>

using namespace mosaic::features::brightness;

namespace {
// Settled levels land within rounding of the curve.
bool near(Levels a, Levels b) {
    return std::abs(a.screen - b.screen) <= 1 && std::abs(a.leds - b.leds) <= 2;
}

// Readings until the levels hold for a few seconds; fails if they never settle.
Levels settle(AutoBrightness& brightness, float lux) {
    Levels last = brightness.update(lux);
    int unchanged = 0;
    for (int i = 0; i < 500 && unchanged < 25; ++i) {
        const Levels next = brightness.update(lux);
        unchanged = next == last ? unchanged + 1 : 0;
        last = next;
    }
    assert(unchanged == 25 && "levels never settled");
    return last;
}

void curve_is_monotonic_and_clamped() {
    // Exact at each point, and held beyond the ends.
    for (const Point& point : kCurve)
        assert(curve(point.lux) == point.levels);
    assert(curve(0) == kCurve[0].levels);
    assert(curve(-5) == kCurve[0].levels);
    assert(curve(100'000) == std::end(kCurve)[-1].levels);
    // Never darker in brighter light, across the whole sensor range.
    Levels previous = curve(0);
    for (float lux = 0.1F; lux < 70'000; lux *= 1.05F) {
        const Levels levels = curve(lux);
        assert(levels.screen >= previous.screen && levels.leds >= previous.leds);
        previous = levels;
    }
    // Between points, levels follow log(lux): halfway in log is halfway in level.
    const float middle = std::sqrt(kCurve[0].lux * kCurve[1].lux);
    const Levels half = curve(middle);
    assert(std::abs(half.screen - (kCurve[0].levels.screen + kCurve[1].levels.screen) / 2) <= 1);
}

void first_reading_applies_at_once() {
    AutoBrightness brightness;
    assert(brightness.update(600) == curve(600));
}

void changes_fade_in_small_steps() {
    AutoBrightness brightness;
    Levels shown = brightness.update(1);
    int readings = 0;
    while (!near(shown, curve(600))) {
        const Levels next = brightness.update(600);
        // Brighter light never dims, and no single reading jumps far.
        assert(next.screen >= shown.screen && next.screen - shown.screen <= 4);
        assert(next.leds >= shown.leds && next.leds - shown.leds <= 12);
        shown = next;
        assert(++readings < 100);
    }
    // A full sweep takes seconds at five readings a second, not one jump.
    assert(readings >= 15);
}

void brief_spikes_are_smoothed() {
    AutoBrightness brightness;
    const Levels dim = settle(brightness, 5);
    // A single bright reading, like a passing reflection, barely moves anything.
    const Levels spike = brightness.update(5'000);
    assert(spike.screen - dim.screen <= 4);
    const Levels after = settle(brightness, 5);
    assert(std::abs(after.screen - dim.screen) <= 4);
}

void light_wavering_near_a_point_does_not_flicker() {
    for (const Point& point : kCurve) {
        AutoBrightness brightness;
        const Levels settled = settle(brightness, point.lux);
        // Light wavering ±15% around the same level changes nothing at all.
        for (int i = 0; i < 500; ++i) {
            const float lux = point.lux * (i % 2 ? 1.15F : 0.85F);
            assert(brightness.update(lux) == settled);
        }
    }
}

void a_change_in_noisy_light_settles_without_flicker() {
    for (const auto& [from, to] : { std::pair { 5.0F, 60.0F }, std::pair { 300.0F, 8.0F } }) {
        AutoBrightness brightness;
        settle(brightness, from);
        // No reading is ever exactly the true light.
        const auto noisy = [](float lux, int i) { return lux * (i % 2 ? 1.15F : 0.87F); };
        Levels shown;
        for (int i = 0; i < 100; ++i)
            shown = brightness.update(noisy(to, i));
        assert(near(shown, curve(to)) || std::abs(shown.screen - curve(to).screen) <= 3);
        // Once there, the same noisy light never moves the levels again.
        for (int i = 0; i < 300; ++i)
            assert(brightness.update(noisy(to, i)) == shown);
    }
}

void a_real_change_is_followed() {
    AutoBrightness brightness;
    settle(brightness, 20);
    // Light doubling is well past the hysteresis, so the levels follow it.
    const Levels brighter = settle(brightness, 40);
    assert(brighter.screen > curve(20).screen);
    assert(near(brighter, curve(40)));
    // And back down again.
    assert(near(settle(brightness, 20), curve(20)));
}

void sensor_failure_fades_to_the_fallback_and_recovers() {
    AutoBrightness brightness;
    settle(brightness, 600);
    // Failed readings fade to the fixed levels instead of jumping or going dark.
    Levels shown = brightness.update(std::nullopt);
    assert(shown.screen < curve(600).screen && shown.screen >= kFallback.screen);
    for (int i = 0; i < 100; ++i)
        shown = brightness.update(std::nullopt);
    assert(shown == kFallback);
    // When readings return, levels follow the light again.
    assert(near(settle(brightness, 1), curve(1)));
    // A sensor missing from the start shows the fallback at once.
    AutoBrightness missing;
    assert(missing.update(std::nullopt) == kFallback);
}

void screen_never_goes_dark() {
    for (float lux : { 0.0F, 0.5F, 1.0F })
        assert(curve(lux).screen > 0 && curve(lux).leds > 0);
    assert(kFallback.screen > 0);
}
} // namespace

int main() {
    curve_is_monotonic_and_clamped();
    first_reading_applies_at_once();
    changes_fade_in_small_steps();
    brief_spikes_are_smoothed();
    light_wavering_near_a_point_does_not_flicker();
    a_real_change_is_followed();
    a_change_in_noisy_light_settles_without_flicker();
    sensor_failure_fades_to_the_fallback_and_recovers();
    screen_never_goes_dark();
}
