#pragma once

#include "hardware.hpp"

#include <SDL.h>

namespace mosaic::preview {

// Draws the outputs of Hardware beside the device screen. The slider supplies
// a simulated sensor reading; it does not compute brightness or LED patterns.
class HardwareView {
public:
    bool start(SDL_Window* device_window);
    void render(const Hardware& hardware, bool speaker_playing);
    void handle(const SDL_Event& event, Hardware& hardware);
    void close();
    uint32_t window_id() const { return window_ ? SDL_GetWindowID(window_) : 0; }

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
};

} // namespace mosaic::preview
