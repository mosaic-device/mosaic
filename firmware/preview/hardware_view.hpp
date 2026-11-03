#pragma once

#include <SDL.h>

namespace mosaic::preview {

// Draws the simulated LEDs, light sensor and speaker beside the device screen. The
// slider sets the sensor's reading; the firmware does everything else.
class HardwareView {
public:
    bool start(SDL_Window* device_window);
    void render();
    void handle(const SDL_Event& event);
    void close();
    uint32_t window_id() const {
        return window_ ? SDL_GetWindowID(window_) : 0;
    }

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
};

} // namespace mosaic::preview
