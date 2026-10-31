#include "policy.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace mosaic::features::brightness {
namespace {
    // Darker than this reads as this, so log10 stays finite.
    constexpr float kMinLux = 0.1F;
    // Each reading moves the smoothed light this far toward the new one: about
    // a second to follow a change at five readings a second.
    constexpr float kSmoothing = 0.2F;
    // The light must move this far, in decades, from where the goal was chosen
    // before the goal changes: about 26%. Light wavering around a point on the
    // curve then never changes the levels back and forth.
    constexpr float kHysteresis = 0.1F;
    // Once following a change, the goal stops when readings are no further than this
    // ahead of the smoothed light, in decades. Noise that lands a reading behind it
    // also stops it, so wavering light never keeps the levels moving.
    constexpr float kCaughtUp = 0.01F;
    // Largest change per reading, so a full sweep fades over about four seconds.
    constexpr int kScreenStep = 4;
    constexpr int kLedStep = 12;

    float light(float lux) {
        return std::log10(std::max(lux, kMinLux));
    }
    uint8_t blend(uint8_t from, uint8_t to, float t) {
        return static_cast<uint8_t>(std::lround(from + (to - from) * t));
    }
    uint8_t step(uint8_t from, uint8_t to, int limit) {
        return static_cast<uint8_t>(from + std::clamp(to - from, -limit, limit));
    }
} // namespace

Levels curve(float lux) {
    const float at = light(lux);
    if (at <= light(kCurve[0].lux))
        return kCurve[0].levels;
    for (size_t i = 1; i < std::size(kCurve); ++i) {
        const float low = light(kCurve[i - 1].lux);
        const float high = light(kCurve[i].lux);
        if (at <= high) {
            const float t = (at - low) / (high - low);
            const Levels& a = kCurve[i - 1].levels;
            const Levels& b = kCurve[i].levels;
            return { .screen = blend(a.screen, b.screen, t), .leds = blend(a.leds, b.leds, t) };
        }
    }
    return std::end(kCurve)[-1].levels;
}

Levels AutoBrightness::update(std::optional<float> lux) {
    if (!lux) {
        sensing_ = false;
        goal_ = kFallback;
    } else {
        const float reading = light(*lux);
        smoothed_ = sensing_ ? smoothed_ + kSmoothing * (reading - smoothed_) : reading;
        if (!sensing_ || std::fabs(smoothed_ - goal_light_) > kHysteresis) {
            following_ = true;
            direction_ = smoothed_ >= goal_light_ ? 1.0F : -1.0F;
        }
        if (following_) {
            goal_light_ = smoothed_;
            goal_ = curve(std::pow(10.0F, smoothed_));
            following_ = (reading - smoothed_) * direction_ > kCaughtUp;
        }
        sensing_ = true;
    }
    // The first levels apply at once; after that, every change fades.
    shown_ = started_ ? Levels { .screen = step(shown_.screen, goal_.screen, kScreenStep),
        .leds = step(shown_.leds, goal_.leds, kLedStep) }
                      : goal_;
    started_ = true;
    return shown_;
}

}
