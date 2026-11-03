#include "device_view.hpp"

#include "console.hpp"
#include "console_log.hpp"
#include "device.hpp"
#include "mosaic_platform.hpp"
#include "ui/generated/assets/assets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>

namespace mosaic::preview {
namespace {
    // The device face, in panel pixels. The physical layout: the ring sits to the right of
    // the screen with the knob in it, the strip runs under both, and the light sensor is
    // on the top edge. Adjust these to match the case.
    constexpr int kSide = 56; // Bezel left and right.
    constexpr int kTop = 72; // Bezel above the screen, holding the light sensor.
    constexpr int kGap = 72; // Between the screen and the ring.
    constexpr int kRingRadius = 190; // Through the ring's LEDs' outer edge.
    constexpr int kKnobRadius = 104;
    constexpr int kStripGap = 52; // Between the screen and the strip.
    constexpr int kStripHeight = 40;
    constexpr int kBottom = 44;
    constexpr int kScreenX = kSide, kScreenY = kTop;
    constexpr int kRingX = kScreenX + device::kWidth + kGap + kRingRadius;
    constexpr int kRingY = kScreenY + device::kHeight / 2;
    constexpr int kBodyWidth = kRingX + kRingRadius + kSide;
    constexpr int kStripY = kScreenY + device::kHeight + kStripGap;
    constexpr int kBodyHeight = kStripY + kStripHeight + kBottom;
    constexpr int kSensorX = kBodyWidth / 2, kSensorY = kTop / 2;

    // The bench panel under the device.
    constexpr int kPanelY = kBodyHeight + 28;
    constexpr int kPad = 32;
    constexpr int kReadoutY = kPanelY + 20;
    constexpr int kSliderY = kReadoutY + 128;
    constexpr int kSliderLeft = kPad + 210, kSliderRight = kBodyWidth - kPad - 230;
    constexpr int kUnplugX = kSliderRight + 40, kUnplugW = 190, kUnplugH = 52;
    constexpr int kLogY = kSliderY + 60;
    constexpr int kLogLines = 7;
    constexpr int kLineHeight = 38;
    constexpr int kInputY = kLogY + kLogLines * kLineHeight + 12;
    constexpr int kInputH = 56;
    constexpr int kHeight = kInputY + kInputH + kPad;

    constexpr float kMaxLux = 600.0F;

    constexpr SDL_Color kInk { 238, 236, 230, 255 };
    constexpr SDL_Color kMuted { 150, 152, 160, 255 };
    constexpr SDL_Color kFaint { 96, 99, 108, 255 };
    constexpr SDL_Color kAccent { 160, 120, 230, 255 };

    void set(SDL_Renderer* renderer, SDL_Color color) {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    }
    void fill(SDL_Renderer* renderer, SDL_Rect rect, SDL_Color color) {
        set(renderer, color);
        SDL_RenderFillRect(renderer, &rect);
    }
    void disc(SDL_Renderer* renderer, int cx, int cy, int radius) {
        for (int y = -radius; y <= radius; ++y) {
            const int x = static_cast<int>(std::lround(std::sqrt(static_cast<float>(radius * radius - y * y))));
            SDL_RenderDrawLine(renderer, cx - x, cy + y, cx + x, cy + y);
        }
    }
    void rounded(SDL_Renderer* renderer, SDL_Rect rect, int radius, SDL_Color color) {
        set(renderer, color);
        const SDL_Rect middle { rect.x, rect.y + radius, rect.w, rect.h - 2 * radius };
        SDL_RenderFillRect(renderer, &middle);
        for (int y = 0; y < radius; ++y) {
            const float dy = static_cast<float>(radius - y) - 0.5F;
            const int inset = radius - static_cast<int>(std::sqrt(static_cast<float>(radius * radius) - dy * dy));
            SDL_RenderDrawLine(renderer, rect.x + inset, rect.y + y, rect.x + rect.w - 1 - inset, rect.y + y);
            SDL_RenderDrawLine(renderer, rect.x + inset, rect.y + rect.h - 1 - y, rect.x + rect.w - 1 - inset,
                rect.y + rect.h - 1 - y);
        }
    }
    // An LED as seen: its limited drive scaled back to full range so the ceiling reads as
    // fully on, with a glow that grows with brightness.
    void led(SDL_Renderer* renderer, int cx, int cy, int radius, drivers::leds::Color color) {
        const auto seen = [](uint8_t value) {
            return static_cast<uint8_t>(std::min(255, value * 255 / std::max<int>(drivers::leds::kCeiling, 1)));
        };
        const SDL_Color lit { seen(color.red), seen(color.green), seen(color.blue), 255 };
        const int peak = std::max({ lit.r, lit.g, lit.b });
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (int ring = 3; ring >= 1 && peak > 0; --ring) {
            set(renderer, { lit.r, lit.g, lit.b, static_cast<uint8_t>(peak / (ring * 6)) });
            disc(renderer, cx, cy, radius + ring * radius / 2);
        }
        set(renderer, { 38, 39, 43, 255 });
        disc(renderer, cx, cy, radius + 3);
        set(renderer, peak ? lit : SDL_Color { 24, 25, 28, 255 });
        disc(renderer, cx, cy, radius);
    }
    bool inside(int x, int y, SDL_Rect rect) {
        return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
    }
    bool on_knob(int x, int y) {
        return std::hypot(x - kRingX, y - kRingY) <= kKnobRadius;
    }
    float lux_at(int x) {
        const float t = std::clamp(static_cast<float>(x - kSliderLeft) / (kSliderRight - kSliderLeft), 0.0F, 1.0F);
        return std::pow(kMaxLux, t);
    }
    int x_of(float lux) {
        const float t = lux <= 1 ? 0 : std::clamp(std::log(lux) / std::log(kMaxLux), 0.0F, 1.0F);
        return kSliderLeft + static_cast<int>(std::lround(t * (kSliderRight - kSliderLeft)));
    }
    const char* network_name(device::Network network) {
        switch (network) {
        case device::Network::joins:
            return "joins";
        case device::Network::rejects:
            return "rejects";
        default:
            return "silent";
        }
    }
    void run_in_background(std::string line) {
        std::thread([line] { run_command(line); }).detach();
    }
} // namespace

int DeviceView::width() {
    return kBodyWidth;
}
int DeviceView::height() {
    return kHeight;
}

bool DeviceView::start(SDL_Renderer* renderer) {
    renderer_ = renderer;
    return renderer_ != nullptr;
}

void DeviceView::close() {
    text_.clear();
}

void DeviceView::render(SDL_Texture* screen) {
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    set(renderer_, { 22, 23, 27, 255 });
    SDL_RenderClear(renderer_);
    draw_device(screen);
    draw_panel();
}

void DeviceView::draw_device(SDL_Texture* screen) {
    rounded(renderer_, { 0, 0, kBodyWidth, kBodyHeight }, 36, { 48, 50, 56, 255 });
    rounded(renderer_, { 3, 3, kBodyWidth - 6, kBodyHeight - 6 }, 34, { 12, 13, 15, 255 });

    // Light sensor on the top edge.
    const bool sensing = device::lux().has_value();
    set(renderer_, { 40, 42, 48, 255 });
    disc(renderer_, kSensorX, kSensorY, 15);
    set(renderer_, sensing ? SDL_Color { 70, 62, 92, 255 } : SDL_Color { 20, 20, 22, 255 });
    disc(renderer_, kSensorX, kSensorY, 10);

    // Screen, dimmed by the backlight.
    const SDL_Rect panel { kScreenX, kScreenY, device::kWidth, device::kHeight };
    fill(renderer_, { panel.x - 6, panel.y - 6, panel.w + 12, panel.h + 12 }, { 4, 4, 5, 255 });
    SDL_RenderCopy(renderer_, screen, nullptr, &panel);
    fill(renderer_, panel, { 0, 0, 0, static_cast<uint8_t>((100 - device::backlight()) * 255 / 100) });

    // Ring around the knob.
    const auto leds = device::leds();
    set(renderer_, { 30, 31, 35, 255 });
    disc(renderer_, kRingX, kRingY, kRingRadius);
    set(renderer_, { 16, 17, 20, 255 });
    disc(renderer_, kRingX, kRingY, kRingRadius - 58);
    constexpr float kPi = 3.14159265358979323846F;
    for (size_t i = 0; i < leds.ring.size(); ++i) {
        const float angle = 2 * kPi * static_cast<float>(i) / static_cast<float>(leds.ring.size()) - kPi / 2;
        led(renderer_, kRingX + static_cast<int>(std::lround((kRingRadius - 29) * std::cos(angle))),
            kRingY + static_cast<int>(std::lround((kRingRadius - 29) * std::sin(angle))), 16, leds.ring[i]);
    }
    set(renderer_, knob_down_ ? SDL_Color { 70, 72, 80, 255 } : SDL_Color { 58, 60, 67, 255 });
    disc(renderer_, kRingX, kRingY, kKnobRadius);
    set(renderer_, { 44, 46, 52, 255 });
    disc(renderer_, kRingX, kRingY, kKnobRadius - 14);

    // Strip from the screen's left edge to the ring's right edge.
    const int strip_left = kScreenX, strip_right = kRingX + kRingRadius;
    fill(renderer_, { strip_left, kStripY, strip_right - strip_left, kStripHeight }, { 26, 27, 31, 255 });
    const float step = static_cast<float>(strip_right - strip_left) / static_cast<float>(leds.strip.size());
    for (size_t i = 0; i < leds.strip.size(); ++i)
        led(renderer_, strip_left + static_cast<int>(step * (static_cast<float>(i) + 0.5F)), kStripY + kStripHeight / 2,
            13, leds.strip[i]);

    // The speaker grille lights while a cue plays.
    const bool playing = device::speaker_on();
    for (int i = 0; i < 5; ++i) {
        set(renderer_, playing ? SDL_Color { 120, 170, 255, 255 } : SDL_Color { 40, 42, 48, 255 });
        disc(renderer_, kRingX - 40 + i * 20, kStripY - 26, 4);
    }
}

void DeviceView::draw_panel() {
    const auto* label = &mosaic_manrope_28_500;
    const auto* body = &mosaic_manrope_28_400;
    rounded(renderer_, { 0, kPanelY, kBodyWidth, kHeight - kPanelY }, 24, { 30, 31, 36, 255 });

    // Readouts.
    char value[64];
    const auto lux = device::lux();
    const auto leds = device::leds();
    struct Readout {
        const char* name;
        std::string text;
    };
    if (lux)
        std::snprintf(value, sizeof(value), "%.0f lux", *lux);
    const Readout readouts[] = {
        { "Light", lux ? value : "unplugged" },
        { "Screen", device::backlight() ? std::to_string(device::backlight()) + "%" : "off" },
        { "LEDs", std::to_string(leds.level * 100 / 255) + "%" },
        { "Speaker", device::speaker_on() ? "playing" : "quiet" },
        { "Saved network", network_name(device::network()) },
        { "Storage", platform::nvs_write_failure() ? "full" : "ok" },
    };
    const int column = (kBodyWidth - 2 * kPad) / static_cast<int>(std::size(readouts));
    for (size_t i = 0; i < std::size(readouts); ++i) {
        const int x = kPad + static_cast<int>(i) * column;
        text_.draw(renderer_, body, readouts[i].name, x, kReadoutY, kMuted);
        text_.draw(renderer_, label, readouts[i].text, x, kReadoutY + 36, kInk);
    }

    // Room light.
    text_.draw(renderer_, body, "Room light", kPad, kSliderY - 18, kMuted);
    fill(renderer_, { kSliderLeft, kSliderY - 3, kSliderRight - kSliderLeft, 6 }, { 70, 72, 82, 255 });
    if (lux) {
        fill(renderer_, { kSliderLeft, kSliderY - 3, x_of(*lux) - kSliderLeft, 6 }, kAccent);
        set(renderer_, kInk);
        disc(renderer_, x_of(*lux), kSliderY, 13);
    }
    rounded(renderer_, { kUnplugX, kSliderY - kUnplugH / 2, kUnplugW, kUnplugH }, 12,
        lux ? SDL_Color { 48, 50, 58, 255 } : kAccent);
    text_.draw(renderer_, label, lux ? "Unplug" : "Plug in", kUnplugX + kUnplugW / 2, kSliderY - 18, kInk,
        Text::Align::center);

    // Console.
    const auto* mono = &mosaic_manrope_28_400;
    const auto lines = console_log::lines(kLogLines);
    for (size_t i = 0; i < lines.size(); ++i)
        text_.draw(renderer_, mono, lines[i], kPad, kLogY + static_cast<int>(i) * kLineHeight,
            lines[i].starts_with("mosaic>") ? kInk : kMuted);
    const SDL_Rect input { kPad, kInputY, kBodyWidth - 2 * kPad, kInputH };
    rounded(renderer_, input, 12, typing_ ? SDL_Color { 64, 52, 92, 255 } : SDL_Color { 40, 41, 47, 255 });
    rounded(renderer_, { input.x + 2, input.y + 2, input.w - 4, input.h - 4 }, 11, { 18, 19, 22, 255 });
    if (typing_) {
        const int typed = text_.draw(renderer_, body, "mosaic> " + line_, input.x + 18, input.y + 10, kInk);
        // A blinking caret.
        if ((SDL_GetTicks() / 500) % 2 == 0)
            fill(renderer_, { input.x + 22 + typed, input.y + 12, 3, input.h - 24 }, kInk);
    } else {
        text_.draw(renderer_, body, "F1 or click here to type a command. \"help\" lists them.", input.x + 18,
            input.y + 10, kFaint);
    }
}

void DeviceView::open_console() {
    typing_ = true;
    SDL_StartTextInput();
}

void DeviceView::finish_command(bool run) {
    if (run) {
        std::printf("mosaic> %s\n", line_.c_str());
        run_in_background(line_);
    }
    line_.clear();
    typing_ = false;
    SDL_StopTextInput();
}

bool DeviceView::handle(const SDL_Event& event) {
    if (typing_ && event.type == SDL_TEXTINPUT) {
        line_ += event.text.text;
        return true;
    }
    if (typing_ && event.type == SDL_KEYDOWN) {
        const auto key = event.key.keysym.sym;
        if (key == SDLK_BACKSPACE && !line_.empty()) {
            // Remove one UTF-8 character.
            do
                line_.pop_back();
            while (!line_.empty() && (static_cast<unsigned char>(line_.back()) & 0xC0) == 0x80);
        } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
            finish_command(!line_.empty());
        } else if (key == SDLK_ESCAPE) {
            finish_command(false);
        }
        return true;
    }
    if (typing_ && event.type == SDL_KEYUP)
        return true;

    if (event.type == SDL_MOUSEWHEEL) {
        int x = 0, y = 0;
        SDL_GetMouseState(&x, &y);
        float logical_x = 0, logical_y = 0;
        SDL_RenderWindowToLogical(renderer_, x, y, &logical_x, &logical_y);
        const int steps = event.wheel.y * (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1);
        if (on_knob(static_cast<int>(logical_x), static_cast<int>(logical_y))) {
            device::turn(steps > 0 ? 1 : steps < 0 ? -1 : 0);
            return true;
        }
        if (std::hypot(logical_x - kSensorX, logical_y - kSensorY) < 40 && device::lux()) {
            device::set_lux(std::clamp(*device::lux() * std::pow(1.25F, static_cast<float>(steps)), 1.0F, kMaxLux));
            return true;
        }
        return false;
    }
    if (event.type != SDL_MOUSEBUTTONDOWN && event.type != SDL_MOUSEBUTTONUP && event.type != SDL_MOUSEMOTION)
        return false;
    const bool down = event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT;
    const bool up = event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT;
    const int x = event.type == SDL_MOUSEMOTION ? event.motion.x : event.button.x;
    const int y = event.type == SDL_MOUSEMOTION ? event.motion.y : event.button.y;
    const SDL_Rect screen { kScreenX, kScreenY, device::kWidth, device::kHeight };

    if (up) {
        if (touching_)
            device::touch(false, x - kScreenX, y - kScreenY);
        if (knob_down_)
            device::button(false);
        touching_ = knob_down_ = dragging_light_ = false;
        return true;
    }
    if (event.type == SDL_MOUSEMOTION) {
        if (touching_)
            device::touch(true, x - kScreenX, y - kScreenY);
        if (dragging_light_)
            device::set_lux(lux_at(x));
        return touching_ || dragging_light_;
    }
    if (!down)
        return false;
    if (inside(x, y, screen)) {
        touching_ = true;
        device::touch(true, x - kScreenX, y - kScreenY);
    } else if (on_knob(x, y)) {
        knob_down_ = true;
        device::button(true);
    } else if (inside(x, y, { kUnplugX, kSliderY - kUnplugH / 2, kUnplugW, kUnplugH })) {
        device::set_lux(device::lux() ? std::nullopt : std::optional<float>(150.0F));
    } else if (y >= kSliderY - 24 && y <= kSliderY + 24 && x >= kSliderLeft - 20 && x <= kSliderRight + 20) {
        dragging_light_ = true;
        device::set_lux(lux_at(x));
    } else if (inside(x, y, { kPad, kInputY, kBodyWidth - 2 * kPad, kInputH })) {
        open_console();
    }
    return true;
}

} // namespace mosaic::preview
