#pragma once
// The device as one window: the screen, the LED ring around the knob to its right, the
// LED strip along the bottom and the light sensor on top, with a bench panel underneath
// whose buttons stand in for the world around the device.
#include "text.hpp"

#include <SDL.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mosaic::preview {

class DeviceView {
public:
    struct Options {
        // Prepared states to restart into, and the one this run started as, if any.
        std::vector<std::string> states;
        std::string current;
        std::function<void(const std::string&)> start_as;
    };

    // The whole window in panel pixels; the screen is 1280 of its width.
    static int width();
    static int height();

    bool start(SDL_Renderer* renderer, Options options);
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
    struct Button {
        SDL_Rect rect;
        std::function<void()> action;
    };
    void draw_device(SDL_Texture* screen);
    void draw_panel();
    // Draws a button with its left edge at x on the row at y; returns its right edge.
    int button(int x, int y, const std::string& label, bool selected, bool enabled, std::function<void()> action);
    int button_width(const std::string& label);
    void row_label(int y, const char* title, const char* hint);
    void finish_command(bool run);

    SDL_Renderer* renderer_ = nullptr;
    Options options_;
    Text text_;
    std::vector<Button> buttons_;
    int mouse_x_ = -1, mouse_y_ = -1;
    // The button the mouse went down on; it acts if the mouse comes up over it too.
    bool pressing_ = false;
    SDL_Rect pressed_ { };
    bool typing_ = false;
    std::string line_;
    bool dragging_light_ = false;
    bool touching_ = false;
    bool knob_down_ = false;
};

} // namespace mosaic::preview
