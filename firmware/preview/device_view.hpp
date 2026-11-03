#pragma once
// The device as one window: the screen, the LED ring around the knob to its right, the
// LED strip along the bottom and the light sensor on top, with a bench panel underneath
// for readouts, the room's light and the console.
#include "text.hpp"

#include <SDL.h>

#include <cstdint>
#include <string>

namespace mosaic::preview {

class DeviceView {
public:
    // The whole window in panel pixels; the screen is 1280 of its width.
    static int width();
    static int height();

    bool start(SDL_Renderer* renderer);
    // Draws everything; screen is the latest frame as a texture.
    void render(SDL_Texture* screen);
    // Handles mouse and text events for the device and panel; true when consumed.
    bool handle(const SDL_Event& event);
    // The console line has the keyboard while open.
    bool typing() const {
        return typing_;
    }
    void open_console();
    void close();

private:
    void draw_device(SDL_Texture* screen);
    void draw_panel();
    void finish_command(bool run);

    SDL_Renderer* renderer_ = nullptr;
    Text text_;
    bool typing_ = false;
    std::string line_;
    bool dragging_light_ = false;
    bool touching_ = false;
    bool knob_down_ = false;
};

} // namespace mosaic::preview
