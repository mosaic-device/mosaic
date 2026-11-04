// The KY-040 as keys and the hardware panel: turning is whole detents, and the button
// goes through the firmware's own press sorting and latch.
#include "../device.hpp"
#include "drivers/encoder.hpp"
#include "drivers/encoder_logic.hpp"
#include "esp_log.h"
#include "esp_timer.h"

#include <atomic>
#include <mutex>

namespace mosaic::drivers::encoder {
namespace {
    constexpr const char* kTag = "mosaic_encoder";
    lv_indev_t* s_indev = nullptr;
    bool s_ready = false;
    std::atomic<int32_t> s_steps { 0 };
    PressLatch s_latch; // LVGL only
    // The button, sorted on the window thread like iot_button's timer task.
    std::mutex s_button_lock;
    PressSorter s_sorter;
    int64_t s_down_at = -1;
    std::atomic<uint32_t> s_clicks { 0 };
    std::atomic<uint32_t> s_backs { 0 };

    void read(lv_indev_t*, lv_indev_data_t* data) {
        data->enc_diff = static_cast<int16_t>(s_steps.exchange(0));
        data->state = s_latch.update(s_clicks.load()) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }
    // With s_button_lock held.
    void count(Press press) {
        if (press == Press::click)
            s_clicks.fetch_add(1);
        else if (press == Press::back)
            s_backs.fetch_add(1);
    }
}

esp_err_t start(lv_display_t* display) {
    static bool attempted = false;
    if (attempted)
        return s_ready ? ESP_OK : ESP_ERR_INVALID_STATE;
    attempted = true;
    lv_group_t* group = lv_group_create();
    s_indev = lv_indev_create();
    if (!group || !s_indev)
        return ESP_ERR_NO_MEM;
    lv_group_set_default(group);
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(s_indev, read);
    lv_indev_set_display(s_indev, display);
    lv_indev_set_group(s_indev, group);
    s_ready = true;
    ESP_LOGI(kTag, "Rotary encoder ready on the keyboard and hardware panel");
    return ESP_OK;
}
void set_group(lv_group_t* group) {
    if (s_ready)
        lv_indev_set_group(s_indev, group);
}
uint32_t back_presses() {
    return s_backs.load();
}
} // namespace mosaic::drivers::encoder

namespace mosaic::preview::device {
namespace encoder = drivers::encoder;

void turn(int detents) {
    encoder::s_steps.fetch_add(detents);
}
void button(bool down) {
    const std::lock_guard lock(encoder::s_button_lock);
    if (down == (encoder::s_down_at >= 0))
        return;
    if (down) {
        encoder::s_down_at = esp_timer_get_time();
        return;
    }
    encoder::s_down_at = -1;
    encoder::count(encoder::s_sorter.released());
}
void poll_button() {
    const std::lock_guard lock(encoder::s_button_lock);
    if (encoder::s_down_at >= 0 && esp_timer_get_time() - encoder::s_down_at >= drivers::encoder::kLongPressMs * 1000) {
        encoder::count(encoder::s_sorter.long_press_started());
        // Once per press: the sorter ignores repeats, and this stops asking.
        encoder::s_down_at = INT64_MAX;
    }
}
void back() {
    const std::lock_guard lock(encoder::s_button_lock);
    encoder::count(encoder::s_sorter.long_press_started());
    encoder::count(encoder::s_sorter.released());
    encoder::s_down_at = -1;
}
} // namespace mosaic::preview::device
