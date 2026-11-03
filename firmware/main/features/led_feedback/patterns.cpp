#include "patterns.hpp"

#include <algorithm>

namespace mosaic::features::led_feedback {
namespace {
    using events::Status;

    // Full-scale colors: the room's brightness level and the driver's ceiling dim them,
    // and full scale keeps their hue intact at the lowest levels.
    constexpr Color kWarmWhite { .red = 255, .green = 160, .blue = 80 };
    constexpr Color kSoftWhite { .red = 255, .green = 235, .blue = 210 };
    constexpr Color kAmber { .red = 255, .green = 110, .blue = 0 };

    constexpr Animation kOff { };
    constexpr Animation kAmbient { .effect = Effect::solid, .color = kWarmWhite };
    // Slow enough to read as waiting, not as an alert.
    constexpr Animation kPairing { .effect = Effect::breathe, .color = kSoftWhite, .period_ms = 3000 };
    constexpr Animation kTrouble { .effect = Effect::solid, .color = kAmber };

    // The darkest point of a breath, out of 255, so it never looks switched off.
    constexpr uint32_t kBreatheFloor = 64;
    // Rising part of the swell; the fall takes as long, and it holds in between.
    constexpr uint32_t kSwellRampMs = 1000;

    constexpr Color scale(Color color, uint32_t level) {
        const auto channel = [level](uint8_t value) { return static_cast<uint8_t>(value * level / 255); };
        return { channel(color.red), channel(color.green), channel(color.blue) };
    }

    // a at 0, b at 255.
    constexpr Color mix(Color a, Color b, uint32_t t) {
        const auto channel = [t](uint8_t from, uint8_t to) {
            return static_cast<uint8_t>((from * (255 - t) + to * t + 127) / 255);
        };
        return { channel(a.red, b.red), channel(a.green, b.green), channel(a.blue, b.blue) };
    }

    // Eases 0-255 in and out, so fades start and end gently.
    constexpr uint32_t ease(uint32_t t) {
        return t * t * (3 * 255 - 2 * t) / (255 * 255);
    }

    // How far along (0-255) elapsed is through duration.
    constexpr uint32_t progress(uint32_t elapsed, uint32_t duration) {
        return elapsed >= duration ? 255 : elapsed * 255 / duration;
    }

    uint32_t breathe_level(uint32_t phase, uint32_t period) {
        const uint32_t distance = std::min(phase, period - phase);
        const uint32_t rise = ease(std::min<uint32_t>(distance * 2 * 255 / period, 255));
        return kBreatheFloor + (255 - kBreatheFloor) * rise / 255;
    }

    // 0-255: how far the strip has swollen toward the notification color.
    uint32_t swell(uint32_t elapsed) {
        if (elapsed < kSwellRampMs)
            return ease(progress(elapsed, kSwellRampMs));
        if (elapsed < kSwellMs - kSwellRampMs)
            return 255;
        return ease(255 - progress(elapsed - (kSwellMs - kSwellRampMs), kSwellRampMs));
    }
} // namespace

Pattern pattern(Status status, bool paired) {
    const Animation strip = paired ? kAmbient : kOff;
    switch (status) {
    case Status::connected:
        return { .ring = kOff, .strip = kAmbient };
    case Status::pairing:
        return { .ring = kPairing, .strip = kOff };
    case Status::error:
        return { .ring = kTrouble, .strip = strip };
    case Status::disconnected:
        break;
    }
    return { };
}

void render(const Animation& animation, uint32_t elapsed_ms, std::span<Color> leds) {
    const uint32_t period = animation.period_ms;
    switch (animation.effect) {
    case Effect::off:
        break;
    case Effect::solid:
        std::ranges::fill(leds, animation.color);
        return;
    case Effect::breathe:
        // A missing period would divide by zero; hold the color steady instead.
        std::ranges::fill(
            leds, period == 0 ? animation.color : scale(animation.color, breathe_level(elapsed_ms % period, period)));
        return;
    }
    std::ranges::fill(leds, Color { });
}

void Feedback::Light::render(const Animation& animation, uint32_t now_ms, std::span<Color> leds) {
    const size_t count = std::min(leds.size(), kMaxLeds);
    if (!started || animation != target) {
        // Fade from what is showing now, even mid-fade, so a change never jumps.
        std::copy_n(shown.begin(), count, from.begin());
        target = animation;
        since = now_ms;
        started = true;
    }
    const uint32_t elapsed = now_ms - since;
    led_feedback::render(target, elapsed, leds.first(count));
    const uint32_t t = ease(progress(elapsed, kFadeMs));
    for (size_t i = 0; i < count; ++i) {
        leds[i] = mix(from[i], leds[i], t);
        shown[i] = leds[i];
    }
    std::ranges::fill(leds.subspan(count), Color { });
}

Feedback::Feedback(Status status, bool paired)
    : status_(status)
    , paired_(paired) { }

void Feedback::set_status(Status status, bool paired) {
    status_ = status;
    paired_ = paired;
    if (!paired_) {
        waiting_ = false;
        swelling_ = false;
    }
}

void Feedback::notify(uint32_t now_ms) {
    waiting_ = true;
    // Another notification mid-swell carries on from the strip's current level instead
    // of dropping back: rising keeps rising, holding holds again, falling rises again.
    const uint32_t elapsed = now_ms - notified_at_;
    if (!swelling_ || elapsed >= kSwellMs)
        notified_at_ = now_ms;
    else if (elapsed >= kSwellMs - kSwellRampMs)
        notified_at_ = now_ms - (kSwellMs - elapsed);
    else if (elapsed >= kSwellRampMs)
        notified_at_ = now_ms - kSwellRampMs;
    swelling_ = true;
}

void Feedback::acknowledge() {
    waiting_ = false;
}

void Feedback::render(uint32_t now_ms, std::span<Color> ring, std::span<Color> strip) {
    const Pattern base = pattern(status_, paired_);
    ring_.render(waiting_ ? kNotificationRing : base.ring, now_ms, ring);
    strip_.render(base.strip, now_ms, strip);
    if (!swelling_)
        return;
    const uint32_t elapsed = now_ms - notified_at_;
    if (elapsed >= kSwellMs) {
        swelling_ = false;
        return;
    }
    const uint32_t amount = swell(elapsed);
    for (Color& led : strip)
        led = mix(led, kNotificationColor, amount);
}

}
