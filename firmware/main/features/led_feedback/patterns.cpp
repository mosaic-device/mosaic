#include "patterns.hpp"

#include <algorithm>

namespace mosaic::features::led_feedback {
namespace {
    using events::Status;

    // Provisional until #56 defines what the LEDs mean. Change the patterns here; tests
    // cover the rules every pattern must follow, not these particular choices.
    constexpr Color kBlue { .red = 0, .green = 90, .blue = 255 };
    constexpr Color kRed { .red = 255, .green = 0, .blue = 0 };
    constexpr Color kWarmWhite { .red = 255, .green = 150, .blue = 70 };
    constexpr Color kCoolWhite { .red = 200, .green = 220, .blue = 255 };

    // Disconnected stays dark: normal builds report it until the M3 connection exists.
    constexpr Pattern kDisconnected { };
    constexpr Pattern kPairing {
        .ring = { .effect = Effect::spin, .color = kBlue, .period_ms = 1200 },
        .strip = { .effect = Effect::breathe, .color = kBlue, .period_ms = 2400 },
    };
    // Idle: a quiet, steady strip with the ring dark.
    constexpr Pattern kConnected {
        .ring = { },
        .strip = { .effect = Effect::solid, .color = { .red = 128, .green = 75, .blue = 35 } },
    };
    constexpr Pattern kError {
        .ring = { .effect = Effect::blink, .color = kRed, .period_ms = 1000 },
        .strip = { .effect = Effect::solid, .color = { .red = 128, .green = 0, .blue = 0 } },
    };
    constexpr Notification kNotification {
        .pattern = {
            .ring = { .effect = Effect::spin, .color = kCoolWhite, .period_ms = 600 },
            .strip = { .effect = Effect::breathe, .color = kWarmWhite, .period_ms = 1000 },
        },
        .duration_ms = 3000,
    };

    // A spinning LED's tail, brightest first.
    constexpr uint8_t kSpinLevels[] = { 255, 64, 16 };

    constexpr Color scale(Color color, uint8_t level) {
        const auto channel = [level](uint8_t value) { return static_cast<uint8_t>(value * level / 255); };
        return { channel(color.red), channel(color.green), channel(color.blue) };
    }

    // 0 at the start of the period, 255 halfway, 0 again at the end. Squared so the fade
    // spends longer near dark, where the eye notices steps most.
    uint8_t breathe_level(uint32_t phase, uint32_t period) {
        const uint32_t distance = std::min(phase, period - phase);
        const uint32_t linear = std::min<uint32_t>(distance * 2 * 255 / period, 255);
        return static_cast<uint8_t>(linear * linear / 255);
    }
} // namespace

const Pattern& pattern(Status status) {
    switch (status) {
    case Status::pairing:
        return kPairing;
    case Status::connected:
        return kConnected;
    case Status::error:
        return kError;
    case Status::disconnected:
        break;
    }
    return kDisconnected;
}

const Notification& notification() {
    return kNotification;
}

void render(const Animation& animation, uint32_t elapsed_ms, std::span<Color> leds) {
    const Color off { };
    const uint32_t period = animation.period_ms;
    // A missing period would divide by zero; hold the color steady instead.
    const Effect effect = period == 0 && animation.effect != Effect::off ? Effect::solid : animation.effect;
    const uint32_t phase = period == 0 ? 0 : elapsed_ms % period;
    switch (effect) {
    case Effect::off:
        std::ranges::fill(leds, off);
        return;
    case Effect::solid:
        std::ranges::fill(leds, animation.color);
        return;
    case Effect::breathe:
        std::ranges::fill(leds, scale(animation.color, breathe_level(phase, period)));
        return;
    case Effect::blink:
        std::ranges::fill(leds, phase * 2 < period ? animation.color : off);
        return;
    case Effect::spin: {
        const size_t count = leds.size();
        const size_t head = phase * count / period;
        for (size_t i = 0; i < count; ++i) {
            const size_t behind = (head + count - i) % count;
            leds[i] = behind < std::size(kSpinLevels) ? scale(animation.color, kSpinLevels[behind]) : off;
        }
        return;
    }
    }
    std::ranges::fill(leds, off);
}

Feedback::Feedback(Status status, uint32_t now_ms)
    : status_(status)
    , status_since_(now_ms) { }

void Feedback::set_status(Status status, uint32_t now_ms) {
    if (status == status_)
        return;
    status_ = status;
    status_since_ = now_ms;
}

void Feedback::notify(uint32_t now_ms) {
    notifying_ = true;
    notified_at_ = now_ms;
}

void Feedback::render(uint32_t now_ms, std::span<Color> ring, std::span<Color> strip) {
    if (notifying_ && now_ms - notified_at_ >= kNotification.duration_ms)
        notifying_ = false;
    const Pattern& shown = notifying_ ? kNotification.pattern : pattern(status_);
    const uint32_t elapsed = now_ms - (notifying_ ? notified_at_ : status_since_);
    led_feedback::render(shown.ring, elapsed, ring);
    led_feedback::render(shown.strip, elapsed, strip);
}

}
