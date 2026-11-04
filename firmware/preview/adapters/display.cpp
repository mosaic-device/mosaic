// The panel as an LVGL display drawing into memory; the window shows the frames.
#include "../device.hpp"
#include "drivers/display.hpp"
#include "esp_log.h"
#include "esp_timer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

namespace mosaic::drivers::display {
namespace {
    constexpr const char* kTag = "mosaic_display";
    constexpr auto kLockTimeout = std::chrono::milliseconds(250);
    // As on the device: the setup handshake treats a lit backlight as a visible screen.
    constexpr uint8_t kMinBrightness = 10;

    std::recursive_timed_mutex s_lock;
    lv_display_t* s_display = nullptr;
    std::atomic_bool s_started { false };
    std::vector<uint16_t> s_frame(preview::device::kWidth* preview::device::kHeight);
    std::atomic_bool s_flushed { false };
    std::atomic_bool s_backlight_on { false };
    std::atomic_bool s_shutting_down { false };
    std::atomic<uint8_t> s_brightness { 100 };

    // Touch changes queue up so a quick tap between two LVGL reads still presses and
    // releases, as the panel's controller reports each change.
    struct Touch {
        bool pressed;
        int32_t x, y;
    };
    std::mutex s_touch_lock;
    std::deque<Touch> s_touches;
    Touch s_touch { false, 0, 0 };

    uint32_t now_ms() {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000);
    }
    void flush(lv_display_t* display, const lv_area_t*, uint8_t*) {
        lv_display_flush_ready(display);
    }
    void read_touch(lv_indev_t*, lv_indev_data_t* data) {
        const std::lock_guard lock(s_touch_lock);
        if (!s_touches.empty()) {
            s_touch = s_touches.front();
            s_touches.pop_front();
        }
        data->point.x = s_touch.x;
        data->point.y = s_touch.y;
        data->state = s_touch.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->continue_reading = !s_touches.empty();
    }
}

esp_err_t start() {
    static bool attempted = false;
    if (attempted)
        return s_display ? ESP_OK : ESP_ERR_INVALID_STATE;
    attempted = true;
    const std::lock_guard lock(s_lock);
    lv_init();
    lv_tick_set_cb(now_ms);
    s_display = lv_display_create(preview::device::kWidth, preview::device::kHeight);
    if (!s_display)
        return ESP_FAIL;
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_display, s_frame.data(), nullptr, static_cast<uint32_t>(s_frame.size() * sizeof(uint16_t)),
        LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(s_display, flush);
    auto* touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, read_touch);
    lv_indev_set_display(touch, s_display);
    s_started = true;
    ESP_LOGI(kTag, "Preview panel %dx%d", preview::device::kWidth, preview::device::kHeight);
    return ESP_OK;
}
lv_display_t* handle() {
    return s_display;
}
bool lock() {
    return s_lock.try_lock_for(kLockTimeout);
}
void unlock() {
    s_lock.unlock();
}
bool available() {
    return s_display && !s_shutting_down.load();
}
void tick() { }
void note_flush() {
    s_flushed = true;
}
bool refresh_ready() {
    if (!s_flushed.load() || s_shutting_down.load())
        return false;
    s_backlight_on = true;
    return true;
}
esp_err_t set_brightness(uint8_t percent) {
    s_brightness = std::clamp<uint8_t>(percent, kMinBrightness, 100);
    return ESP_OK;
}
void prepare_reboot() {
    s_shutting_down = true;
    s_backlight_on = false;
}
} // namespace mosaic::drivers::display

namespace mosaic::preview::device {
namespace display = drivers::display;

bool display_started() {
    return display::s_started.load();
}
void run_display() {
    if (!display_started())
        return;
    const std::lock_guard lock(display::s_lock);
    lv_timer_handler();
}
void copy_frame(std::span<uint16_t, kWidth * kHeight> frame) {
    const std::lock_guard lock(display::s_lock);
    std::memcpy(frame.data(), display::s_frame.data(), frame.size_bytes());
}
uint8_t backlight() {
    return display::s_backlight_on.load() && !display::s_shutting_down.load() ? display::s_brightness.load() : 0;
}
void touch(bool pressed, int x, int y) {
    const std::lock_guard lock(display::s_touch_lock);
    const display::Touch next { pressed, std::clamp(x, 0, kWidth - 1), std::clamp(y, 0, kHeight - 1) };
    auto& queue = display::s_touches;
    // Moves while held or released only matter as the latest position.
    if (!queue.empty() && queue.back().pressed == pressed)
        queue.back() = next;
    else
        queue.push_back(next);
}
} // namespace mosaic::preview::device
