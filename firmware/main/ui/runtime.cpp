#include "runtime.hpp"
#include "render_ack.hpp"
#include "../drivers/display.hpp"
#include "../drivers/encoder.hpp"
#include "../support/secure_erase.hpp"
#include "generated/design_tokens.h"
#include <atomic>
#include <cstring>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
namespace mosaic::ui {
#ifdef MOSAIC_DISPLAY_DIAGNOSTICS
void log_display_diagnostics();
#endif
namespace {
    namespace display = drivers::display;
    constexpr size_t kCredentialCapacity = 320;
    constexpr int64_t kTimeoutUs = 2'000'000;
    struct Slot {
        uint32_t token = 0;
        size_t size = 0;
        alignas(std::max_align_t) unsigned char bytes[kCredentialCapacity] { };
    };
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    Slot slot;
    RenderAck gate;
    Renderer renderer { };
    std::atomic_bool initialized { false };
    std::atomic_bool waiting { false };
    uint32_t rendered_token = 0; // LVGL worker only
    uint32_t now_ms() {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000);
    }
    void clear(uint32_t token) {
        portENTER_CRITICAL(&mux);
        if (slot.token == token)
            support::secure_erase(slot);
        portEXIT_CRITICAL(&mux);
    }
    void cancel_rendered() {
        renderer.cancel_credentials(renderer.context);
        rendered_token = 0;
    }
    void render_next(lv_timer_t*) {
        display::tick();
        if (!ready())
            return;
        if (rendered_token) {
            if (gate.completed(rendered_token))
                rendered_token = 0;
            else if (gate.pending(rendered_token))
                return;
            else if (gate.completed(rendered_token))
                // Completion is published before the active token is released;
                // recheck if the waiter committed between the two observations.
                rendered_token = 0;
            else
                cancel_rendered();
        }
        Slot work { };
        portENTER_CRITICAL(&mux);
        work = slot;
        support::secure_erase(slot);
        portEXIT_CRITICAL(&mux);
        if (work.token && gate.current(work.token, now_ms())) {
            if (renderer.credentials(work.bytes, work.size, renderer.context)) {
                rendered_token = work.token;
                if (!gate.current(work.token, now_ms()))
                    cancel_rendered();
            } else {
                gate.cancel(work.token);
                renderer.cancel_credentials(renderer.context);
            }
        }
        support::secure_erase(work);
        if (!rendered_token)
            renderer.routine(renderer.context);
    }
    void flush(lv_event_t*) {
        display::note_flush();
        gate.note_flush(rendered_token, now_ms());
    }
    void refresh(lv_event_t*) {
        if (display::refresh_ready())
            gate.acknowledge_refresh(rendered_token, now_ms());
    }
}
bool ready() {
    return initialized.load() && display::available();
}
esp_err_t start(Renderer callbacks) {
    static bool attempted = false;
    if (attempted)
        return ready() ? ESP_OK : ESP_ERR_INVALID_STATE;
    attempted = true;
    if (!callbacks.create || !callbacks.routine || !callbacks.credentials || !callbacks.cancel_credentials)
        return ESP_ERR_INVALID_ARG;
    const esp_err_t err = display::start();
    if (err != ESP_OK)
        return err;
    if (!display::lock())
        return ESP_ERR_TIMEOUT;
    // Before any screen exists, so its focusable widgets join the encoder's default group.
    if (drivers::encoder::start(display::handle()) != ESP_OK)
        ESP_LOGE("mosaic_ui", "Rotary encoder unavailable; continuing with touch input only");
    renderer = callbacks;
    auto* screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_hex(TOKEN_COLOR_BACKGROUND_DARK), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, TOKEN_SPACE_8, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (!renderer.create(screen, renderer.context) || !lv_timer_create(render_next, 10, nullptr)) {
        display::unlock();
        return ESP_ERR_NO_MEM;
    }
    lv_screen_load(screen);
    lv_display_add_event_cb(display::handle(), flush, LV_EVENT_FLUSH_FINISH, nullptr);
    lv_display_add_event_cb(display::handle(), refresh, LV_EVENT_REFR_READY, nullptr);
    display::unlock();
    initialized.store(true);
    return ESP_OK;
}
esp_err_t render_credentials(const void* payload, size_t size) {
    if (!ready())
        return ESP_ERR_INVALID_STATE;
    if (!payload || !size || size > kCredentialCapacity)
        return ESP_ERR_INVALID_ARG;
    bool expected = false;
    if (!waiting.compare_exchange_strong(expected, true))
        return ESP_ERR_INVALID_STATE;
    const int64_t deadline = esp_timer_get_time() + kTimeoutUs;
    const uint32_t token = gate.begin(static_cast<uint32_t>(deadline / 1000));
    portENTER_CRITICAL(&mux);
    support::secure_erase(slot);
    slot.token = token;
    slot.size = size;
    std::memcpy(slot.bytes, payload, size);
    portEXIT_CRITICAL(&mux);
    while (ready() && gate.current(token, now_ms()) && esp_timer_get_time() < deadline) {
        if (gate.complete(token, now_ms())) {
            clear(token);
            waiting.store(false);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10) > 0 ? pdMS_TO_TICKS(10) : 1);
    }
    gate.cancel(token);
    clear(token);
    waiting.store(false);
    ESP_LOGE("mosaic_display", "Credential render acknowledgement timed out or display unavailable");
#ifdef MOSAIC_DISPLAY_DIAGNOSTICS
    log_display_diagnostics();
#endif
    return ESP_ERR_TIMEOUT;
}
}
