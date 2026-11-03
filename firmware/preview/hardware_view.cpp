#include "hardware_view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace mosaic::preview {
namespace {
    constexpr int kWidth = 360;
    constexpr int kHeight = 460;
    constexpr int kSliderLeft = 30;
    constexpr int kSliderRight = 330;
    constexpr int kSliderY = 395;

    void color(SDL_Renderer* renderer, drivers::leds::Color value) {
        SDL_SetRenderDrawColor(renderer, value.red, value.green, value.blue, 255);
    }
    void disc(SDL_Renderer* renderer, int cx, int cy, int radius) {
        for (int y = -radius; y <= radius; ++y) {
            const int x = std::lround(std::sqrt(radius * radius - y * y));
            SDL_RenderDrawLine(renderer, cx - x, cy + y, cx + x, cy + y);
        }
    }
    float lux_at(int x) {
        const float t = std::clamp(static_cast<float>(x - kSliderLeft) / (kSliderRight - kSliderLeft), 0.0F, 1.0F);
        return std::pow(600.0F, t);
    }
} // namespace

bool HardwareView::start(SDL_Window* device_window) {
    int x = 0, y = 0, width = 0, height = 0;
    SDL_GetWindowPosition(device_window, &x, &y);
    SDL_GetWindowSize(device_window, &width, &height);
    window_ = SDL_CreateWindow("Mosaic hardware", x + width + 16, y, kWidth, kHeight,
        SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_)
        return false;
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_)
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    return renderer_ && SDL_RenderSetLogicalSize(renderer_, kWidth, kHeight) == 0;
}

void HardwareView::render(const Hardware& hardware, bool speaker_playing) {
    if (!renderer_)
        return;
    SDL_SetRenderDrawColor(renderer_, 12, 13, 16, 255);
    SDL_RenderClear(renderer_);
    SDL_SetRenderDrawColor(renderer_, 53, 56, 64, 255);
    disc(renderer_, 180, 145, 78);
    SDL_SetRenderDrawColor(renderer_, 18, 19, 23, 255);
    disc(renderer_, 180, 145, 67);
    constexpr float kPi = 3.14159265358979323846F;
    for (size_t i = 0; i < hardware.ring().size(); ++i) {
        const float angle = 2 * kPi * i / hardware.ring().size() - kPi / 2;
        const int cx = std::lround(180 + 74 * std::cos(angle));
        const int cy = std::lround(145 + 74 * std::sin(angle));
        SDL_SetRenderDrawColor(renderer_, 47, 48, 51, 255);
        disc(renderer_, cx, cy, 15);
        color(renderer_, hardware.ring()[i]);
        disc(renderer_, cx, cy, 11);
    }
    SDL_SetRenderDrawColor(renderer_, 49, 50, 54, 255);
    SDL_Rect strip_back { 20, 294, 320, 38 };
    SDL_RenderFillRect(renderer_, &strip_back);
    for (size_t i = 0; i < hardware.strip().size(); ++i) {
        SDL_Rect led { 25 + static_cast<int>(i) * 26, 300, 20, 26 };
        color(renderer_, hardware.strip()[i]);
        SDL_RenderFillRect(renderer_, &led);
    }
    SDL_SetRenderDrawColor(renderer_, 87, 91, 103, 255);
    SDL_Rect track { kSliderLeft, kSliderY - 3, kSliderRight - kSliderLeft, 6 };
    SDL_RenderFillRect(renderer_, &track);
    const float lux = hardware.lux().value_or(0);
    const float position = lux <= 1 ? 0 : std::clamp(std::log(lux) / std::log(600.0F), 0.0F, 1.0F);
    const int thumb_x = std::lround(kSliderLeft + position * (kSliderRight - kSliderLeft));
    SDL_SetRenderDrawColor(renderer_, hardware.lux() ? 247 : 180, hardware.lux() ? 200 : 75, 70, 255);
    disc(renderer_, thumb_x, kSliderY, 11);
    SDL_SetRenderDrawColor(renderer_, speaker_playing ? 112 : 55, speaker_playing ? 190 : 57,
        speaker_playing ? 255 : 63, 255);
    SDL_Rect speaker { 163, 431, 34, 15 };
    SDL_RenderFillRect(renderer_, &speaker);
    SDL_RenderPresent(renderer_);

    char title[160];
    if (hardware.lux())
        std::snprintf(title, sizeof(title), "Mosaic hardware | %.0f lux | screen %u%% | LEDs %u/255", lux,
            hardware.screen_percent(), hardware.led_level());
    else
        std::snprintf(title, sizeof(title), "Mosaic hardware | sensor unavailable | screen %u%% | LEDs %u/255",
            hardware.screen_percent(), hardware.led_level());
    SDL_SetWindowTitle(window_, title);
}

void HardwareView::handle(const SDL_Event& event, Hardware& hardware) {
    if (event.type != SDL_MOUSEBUTTONDOWN && event.type != SDL_MOUSEMOTION)
        return;
    const bool dragging = event.type == SDL_MOUSEBUTTONDOWN ? event.button.button == SDL_BUTTON_LEFT
                                                          : (event.motion.state & SDL_BUTTON_LMASK) != 0;
    if (!dragging)
        return;
    const int x = event.type == SDL_MOUSEMOTION ? event.motion.x : event.button.x;
    const int y = event.type == SDL_MOUSEMOTION ? event.motion.y : event.button.y;
    if (y >= kSliderY - 20 && y <= kSliderY + 20)
        hardware.set_lux(lux_at(x));
}

void HardwareView::close() {
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

} // namespace mosaic::preview
