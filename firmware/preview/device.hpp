#pragma once
// The simulated hardware around the real firmware. The adapters in adapters/ implement
// the firmware's driver and network interfaces on top of this; the window and console
// drive it like a person, a room and a network would.
#include "drivers/leds.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace mosaic::preview::device {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;

// Display. run_display() is the device's LVGL task: it runs LVGL under the display
// lock. Call it from the window thread only.
bool display_started();
void run_display();
// Copies the last rendered frame (RGB565, kWidth x kHeight) under the display lock.
void copy_frame(std::span<uint16_t, kWidth * kHeight> frame);
// The backlight in percent; 0 while dark, before startup lights it or after restart blanks it.
uint8_t backlight();
// The panel's touch controller, in panel pixels.
void touch(bool pressed, int x, int y);

// Rotary encoder. Turning moves whole detents; the button sorts presses into clicks and
// Back with the firmware's own rules, so call poll_button() every frame to time a hold.
void turn(int detents);
void button(bool down);
void poll_button();
// A full long press, for a key that means Back.
void back();

// LEDs as the strip and ring show them, after the firmware's brightness limit.
struct Leds {
    std::array<drivers::leds::Color, drivers::leds::kRingLength> ring;
    std::array<drivers::leds::Color, drivers::leds::kStripLength> strip;
    uint8_t level;
};
Leds leds();

// The BH1750; nullopt makes it stop answering, as if unplugged.
void set_lux(std::optional<float> lux);
std::optional<float> lux();

// Speaker. Without audio the cues still take their real time, silently.
void enable_audio();
bool speaker_on();

// The saved network's behavior when the device joins it.
enum class Network : uint8_t {
    joins, // Associates, then gets an address.
    rejects, // Refuses the device, like a wrong password.
    silent, // Never answers, like a network out of range.
};
void set_network(Network network);
Network network();
// Drops a joined network, like a router restarting.
void drop_network();

// The phone's view of the setup portal: submits a form like the setup page would and
// returns the portal's answer. Only works while the device shows setup.
std::string submit_setup(std::string_view form_body);
std::string setup_status();

} // namespace mosaic::preview::device
