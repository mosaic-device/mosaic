#include "device_view.hpp"

#include "console.hpp"
#include "device.hpp"
#include "events/bus.hpp"
#include "mosaic_platform.hpp"
#include "ui/generated/assets/assets.h"

#include <algorithm>
#include <cctype>
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
    // The light sensor and the speaker grille share the top edge, side by side.
    constexpr int kSensorX = kBodyWidth / 2 - 50, kSensorY = kTop / 2;
    constexpr int kSpeakerX = kBodyWidth / 2 + 10, kSpeakerY = kTop / 2;
    // Empty space around the device and panel inside the window.
    constexpr int kMargin = 48;

    // The bench panel under the device: a row per thing around it, label on the left.
    constexpr int kPanelY = kBodyHeight + 28;
    constexpr int kPad = 40;
    constexpr int kLabelWidth = 370;
    constexpr int kControlsX = kPad + kLabelWidth;
    constexpr int kRight = kBodyWidth - kPad;
    constexpr int kStatusY = kPanelY + 26;
    constexpr int kFirstRowY = kStatusY + 64;
    constexpr int kRowHeight = 64;
    constexpr int kRowStep = kRowHeight + 26;
    constexpr int kButtonGap = 10;
    constexpr int kGroupGap = 40;
    constexpr int kStartRow = kFirstRowY;
    constexpr int kDeviceRow = kStartRow + kRowStep;
    constexpr int kServerRow = kDeviceRow + kRowStep;
    constexpr int kNetworkRow = kServerRow + kRowStep;
    constexpr int kPhoneRow = kNetworkRow + kRowStep;
    constexpr int kRoomRow = kPhoneRow + kRowStep;
    constexpr int kSliderY = kRoomRow + kRowHeight / 2;
    // Room for either label of the sensor button.
    constexpr int kUnplugW = 270;
    constexpr int kUnplugX = kRight - kUnplugW;
    constexpr int kSliderLeft = kControlsX + 16, kSliderRight = kUnplugX - 48;
    constexpr int kHeight = kRoomRow + kRowHeight + 36;

    constexpr float kMaxLux = 600.0F;

    constexpr SDL_Color kBackground { 22, 23, 27, 255 };
    constexpr SDL_Color kInk { 238, 236, 230, 255 };
    constexpr SDL_Color kMuted { 150, 152, 160, 255 };
    constexpr SDL_Color kFaint { 96, 99, 108, 255 };
    constexpr SDL_Color kAccent { 116, 68, 184, 255 };
    constexpr SDL_Color kAccentHover { 132, 86, 200, 255 };
    constexpr SDL_Color kButton { 44, 46, 54, 255 };
    constexpr SDL_Color kButtonHover { 56, 58, 68, 255 };
    constexpr SDL_Color kButtonDisabled { 34, 35, 41, 255 };

    void set(SDL_Renderer* renderer, SDL_Color color) {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    }
    void fill(SDL_Renderer* renderer, SDL_Rect rect, SDL_Color color) {
        set(renderer, color);
        SDL_RenderFillRect(renderer, &rect);
    }
    // One row of a shape.
    void row(SDL_Renderer* renderer, int x1, int y, int x2) {
        const SDL_Rect span { x1, y, x2 - x1 + 1, 1 };
        SDL_RenderFillRect(renderer, &span);
    }
    void disc(SDL_Renderer* renderer, int cx, int cy, int radius) {
        for (int y = -radius; y <= radius; ++y) {
            const int x = static_cast<int>(std::lround(std::sqrt(static_cast<float>(radius * radius - y * y))));
            row(renderer, cx - x, cy + y, cx + x);
        }
    }
    void rounded(SDL_Renderer* renderer, SDL_Rect rect, int radius, SDL_Color color) {
        set(renderer, color);
        const SDL_Rect middle { rect.x, rect.y + radius, rect.w, rect.h - 2 * radius };
        SDL_RenderFillRect(renderer, &middle);
        for (int y = 0; y < radius; ++y) {
            const float dy = static_cast<float>(radius - y) - 0.5F;
            const int inset = radius - static_cast<int>(std::sqrt(static_cast<float>(radius * radius) - dy * dy));
            row(renderer, rect.x + inset, rect.y + y, rect.x + rect.w - 1 - inset);
            row(renderer, rect.x + inset, rect.y + rect.h - 1 - y, rect.x + rect.w - 1 - inset);
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
    std::string title_case(std::string text) {
        if (!text.empty())
            text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        return text;
    }
    void run_in_background(std::string line) {
        std::thread([line] { run_command(line); }).detach();
    }
} // namespace

int DeviceView::width() {
    return kBodyWidth + 2 * kMargin;
}
int DeviceView::height() {
    return kHeight + 2 * kMargin;
}

bool DeviceView::start(SDL_Renderer* renderer, Options options) {
    renderer_ = renderer;
    options_ = std::move(options);
    return renderer_ != nullptr;
}

void DeviceView::close() {
    text_.clear();
    if (canvas_)
        SDL_DestroyTexture(canvas_);
    canvas_ = nullptr;
}

void DeviceView::render(SDL_Texture* screen) {
    // Draw at full size offscreen, then scale the picture into the window: fine detail like
    // one-pixel rows would vanish if each shape were scaled on its own.
    if (!canvas_) {
        canvas_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, kBodyWidth, kHeight);
        if (!canvas_)
            return;
    }
    SDL_SetRenderTarget(renderer_, canvas_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    set(renderer_, kBackground);
    SDL_RenderClear(renderer_);
    draw_device(screen);
    draw_panel();
    SDL_SetRenderTarget(renderer_, nullptr);

    // Centered in the window, inset by the margin.
    set(renderer_, kBackground);
    SDL_RenderClear(renderer_);
    int output_width = 0, output_height = 0;
    SDL_GetRendererOutputSize(renderer_, &output_width, &output_height);
    scale_ = std::min(static_cast<float>(output_width) / static_cast<float>(width()),
        static_cast<float>(output_height) / static_cast<float>(height()));
    offset_x_ = (static_cast<float>(output_width) / scale_ - static_cast<float>(width())) / 2 + kMargin;
    offset_y_ = (static_cast<float>(output_height) / scale_ - static_cast<float>(height())) / 2 + kMargin;
    const SDL_Rect target { static_cast<int>(std::lround(offset_x_ * scale_)),
        static_cast<int>(std::lround(offset_y_ * scale_)), static_cast<int>(std::lround(kBodyWidth * scale_)),
        static_cast<int>(std::lround(kHeight * scale_)) };
    SDL_RenderCopy(renderer_, canvas_, nullptr, &target);
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

    // The speaker grille on the top edge lights while a cue plays.
    const bool playing = device::speaker_on();
    for (int i = 0; i < 5; ++i) {
        set(renderer_, playing ? SDL_Color { 120, 170, 255, 255 } : SDL_Color { 40, 42, 48, 255 });
        disc(renderer_, kSpeakerX + i * 20, kSpeakerY, 4);
    }
}

int DeviceView::button_width(const std::string& label) {
    return std::max(150, text_.width(&mosaic_manrope_28_500, label) + 56);
}

int DeviceView::button(
    int x, int y, const std::string& label, bool selected, bool enabled, std::function<void()> action) {
    const SDL_Rect rect { x, y, button_width(label), kRowHeight };
    const bool hover = enabled && inside(mouse_x_, mouse_y_, rect);
    const bool held = hover && pressing_ && pressed_.x == rect.x && pressed_.y == rect.y;
    SDL_Color fill_color = !enabled ? kButtonDisabled
        : selected                  ? (hover ? kAccentHover : kAccent)
        : hover                     ? kButtonHover
                                    : kButton;
    if (held)
        fill_color = { static_cast<uint8_t>(fill_color.r - 8), static_cast<uint8_t>(fill_color.g - 8),
            static_cast<uint8_t>(fill_color.b - 8), 255 };
    rounded(renderer_, rect, 14, fill_color);
    text_.draw(renderer_, &mosaic_manrope_28_500, label, rect.x + rect.w / 2, rect.y + (kRowHeight - 38) / 2,
        enabled ? kInk : kFaint, Text::Align::center);
    if (enabled)
        buttons_.push_back({ rect, std::move(action) });
    return rect.x + rect.w;
}

void DeviceView::row_label(int y, const char* title, const char* hint) {
    text_.draw(renderer_, &mosaic_manrope_28_500, title, kPad, y + 2, kInk, Text::Align::left, 0.95F);
    text_.draw(renderer_, &mosaic_manrope_28_400, hint, kPad, y + 36, kFaint, Text::Align::left, 0.78F);
}

void DeviceView::draw_panel() {
    const auto* body = &mosaic_manrope_28_400;
    rounded(renderer_, { 0, kPanelY, kBodyWidth, kHeight - kPanelY }, 24, { 30, 31, 36, 255 });
    buttons_.clear();

    // What the device is doing, in one quiet line.
    const auto lux = device::lux();
    const auto leds = device::leds();
    char light[32] = "unplugged";
    if (lux)
        std::snprintf(light, sizeof(light), "%.0f lux", *lux);
    const std::string status = "Screen " + (device::backlight() ? std::to_string(device::backlight()) + "%" : "off")
        + "      LEDs " + std::to_string(leds.level * 100 / 255) + "%      Speaker "
        + (device::speaker_on() ? "playing" : "quiet") + "      Light sensor " + light;
    text_.draw(renderer_, body, status, kPad, kStatusY, kMuted, Text::Align::left, 0.8F);

    // Restart into a prepared device, or restart this one.
    row_label(kStartRow, "Start as", "Restarts into a prepared device");
    int x = kControlsX;
    for (const auto& state : options_.states)
        x = button(x, kStartRow, title_case(state), state == options_.current, true, [this, state] {
            options_.start_as(state);
        }) + kButtonGap;

    // This device: restart it, erase it, or make its storage fail.
    row_label(kDeviceRow, "This device", "Keeps or erases what it saved");
    x = button(kControlsX, kDeviceRow, "Restart", false, true, [] { run_in_background("restart"); }) + kButtonGap;
    x = button(x, kDeviceRow, "Factory reset", false, true, [] { run_in_background("erase"); });
    const bool full = platform::nvs_write_failure();
    x += kGroupGap + 40;
    x += text_.draw(renderer_, body, "Storage", x, kDeviceRow + 13, kMuted, Text::Align::left, 0.85F) + 24;
    x = button(x, kDeviceRow, "Works", !full, true, [] { run_in_background("storage ok"); }) + kButtonGap;
    button(x, kDeviceRow, "Full", full, true, [] { run_in_background("storage full"); });

    // What the server tells the device.
    row_label(kServerRow, "Server", "What the connection reports");
    x = kControlsX;
    const auto current = events::status();
    for (const auto status_choice :
        { events::Status::pairing, events::Status::connected, events::Status::disconnected, events::Status::error }) {
        const std::string name = events::name(status_choice);
        x = button(x, kServerRow, title_case(name), current == status_choice, true, [name] {
            run_in_background("trigger " + name);
        }) + kButtonGap;
    }
    button(x + kGroupGap - kButtonGap, kServerRow, "Send notification", false, true,
        [] { run_in_background("trigger notification"); });

    // How the saved network answers.
    row_label(kNetworkRow, "Saved Wi-Fi", "How the network answers");
    x = kControlsX;
    const auto network = device::network();
    const struct {
        const char* label;
        const char* command;
        device::Network value;
    } networks[] = {
        { "Joins", "wifi joins", device::Network::joins },
        { "Rejects", "wifi rejects", device::Network::rejects },
        { "No answer", "wifi silent", device::Network::silent },
    };
    for (const auto& choice : networks) {
        const std::string command = choice.command;
        x = button(x, kNetworkRow, choice.label, network == choice.value, true, [command] {
            run_in_background(command);
        }) + kButtonGap;
    }
    button(x + kGroupGap - kButtonGap, kNetworkRow, "Drop connection", false, true,
        [] { run_in_background("wifi drop"); });

    // A phone on the setup network.
    row_label(kPhoneRow, "Phone", "The setup page, on this computer");
    const std::string setup = device::setup_status();
    const bool open = !setup.starts_with("No setup");
    const std::string page = device::setup_page();
    x = button(kControlsX, kPhoneRow, "Open setup page", false, open && !page.empty(), [page] {
        SDL_OpenURL(page.c_str());
    }) + kButtonGap;
    x = button(x, kPhoneRow, "Submit Wi-Fi \"Home\"", false, open,
        [] { run_in_background("setup Home home-password wss://mosaic.example.org/device"); });
    const std::string phone = device::setup_page_for_phone();
    const std::string detail = !open ? "Available while the device shows setup"
        : phone.empty()              ? setup
                                     : setup + "   Phone on this network: " + phone;
    text_.draw(renderer_, body, detail, x + 28, kPhoneRow + 13, open ? kMuted : kFaint, Text::Align::left, 0.85F);

    // The room's light at the sensor.
    row_label(kRoomRow, "Room light", "At the sensor on top");
    fill(renderer_, { kSliderLeft, kSliderY - 4, kSliderRight - kSliderLeft, 8 }, { 58, 60, 70, 255 });
    if (lux) {
        fill(renderer_, { kSliderLeft, kSliderY - 4, x_of(*lux) - kSliderLeft, 8 }, kAccent);
        set(renderer_, kInk);
        disc(renderer_, x_of(*lux), kSliderY, 15);
        text_.draw(renderer_, body, "dark", kSliderLeft, kSliderY + 16, kFaint, Text::Align::left, 0.7F);
        text_.draw(renderer_, body, "bright", kSliderRight, kSliderY + 16, kFaint, Text::Align::right, 0.7F);
    } else {
        text_.draw(renderer_, body, "The sensor is unplugged; the device uses a fixed brightness",
            (kSliderLeft + kSliderRight) / 2, kSliderY - 40, kFaint, Text::Align::center, 0.8F);
    }
    const std::string sensor_label = lux ? "Unplug sensor" : "Plug in sensor";
    button(kRight - button_width(sensor_label), kRoomRow, sensor_label, !lux, true,
        [plugged = lux.has_value()] { device::set_lux(plugged ? std::nullopt : std::optional<float>(150.0F)); });
}

void DeviceView::to_device(int window_x, int window_y, int& x, int& y) const {
    // Window points, as SDL_GetMouseState reports them, to output pixels (they differ on
    // high-DPI displays), then into the device's coordinates.
    int window_width = 1, window_height = 1, output_width = 1, output_height = 1;
    SDL_GetWindowSize(SDL_RenderGetWindow(renderer_), &window_width, &window_height);
    SDL_GetRendererOutputSize(renderer_, &output_width, &output_height);
    const float pixel_x
        = static_cast<float>(window_x) * static_cast<float>(output_width) / static_cast<float>(window_width);
    const float pixel_y
        = static_cast<float>(window_y) * static_cast<float>(output_height) / static_cast<float>(window_height);
    x = static_cast<int>(std::floor(pixel_x / scale_ - offset_x_));
    y = static_cast<int>(std::floor(pixel_y / scale_ - offset_y_));
}

bool DeviceView::handle(const SDL_Event& event) {
    if (event.type == SDL_MOUSEWHEEL) {
        int mouse_x = 0, mouse_y = 0;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        int x = 0, y = 0;
        to_device(mouse_x, mouse_y, x, y);
        const float logical_x = static_cast<float>(x), logical_y = static_cast<float>(y);
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
    // Not the event's own coordinates: SDL already rescales those for the renderer, and how
    // depends on the display. The mouse state is always in window points.
    int window_x = 0, window_y = 0;
    SDL_GetMouseState(&window_x, &window_y);
    int x = 0, y = 0;
    to_device(window_x, window_y, x, y);
    mouse_x_ = x;
    mouse_y_ = y;
    const SDL_Rect screen { kScreenX, kScreenY, device::kWidth, device::kHeight };

    if (up) {
        if (touching_)
            device::touch(false, x - kScreenX, y - kScreenY);
        if (knob_down_)
            device::button(false);
        // A button acts when released over it, like any desktop button.
        if (pressing_)
            for (const auto& released : buttons_)
                if (released.rect.x == pressed_.x && released.rect.y == pressed_.y && inside(x, y, released.rect)) {
                    released.action();
                    break;
                }
        pressing_ = false;
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
    for (size_t i = 0; i < buttons_.size(); ++i)
        if (inside(x, y, buttons_[i].rect)) {
            pressing_ = true;
            pressed_ = buttons_[i].rect;
            return true;
        }
    if (inside(x, y, screen)) {
        touching_ = true;
        device::touch(true, x - kScreenX, y - kScreenY);
    } else if (on_knob(x, y)) {
        knob_down_ = true;
        device::button(true);
    } else if (device::lux() && y >= kSliderY - 28 && y <= kSliderY + 28 && x >= kSliderLeft - 20
        && x <= kSliderRight + 20) {
        dragging_light_ = true;
        device::set_lux(lux_at(x));
    }
    return true;
}

} // namespace mosaic::preview
