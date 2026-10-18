// Compile production transport directly so tests can inspect erased internal slots.
// These fakes model only scheduling and display notifications, not LVGL widgets.
#include "../runtime.cpp"
#include <cassert>
#include <string_view>
namespace {
int64_t clock_us = 0;
void (*timer_callback)(lv_timer_t*) = nullptr;
void (*flush_callback)(lv_event_t*) = nullptr;
void (*refresh_callback)(lv_event_t*) = nullptr;
bool available = true;
bool light_ok = true;
bool pump = true;
bool send_flush = true;
bool send_refresh = true;
bool render_ok = true;
bool delay_waiter_past_deadline = false;
bool credentials_visible = false;
int rendered = 0;
int cancelled = 0;
int routine_calls = 0;
void step() {
    if (timer_callback)
        timer_callback(nullptr);
    if (send_flush && flush_callback)
        flush_callback(nullptr);
    if (send_refresh && refresh_callback)
        refresh_callback(nullptr);
}
bool create(lv_obj_t*, void*) {
    return true;
}
void routine(void*) {
    ++routine_calls;
}
bool credentials(const void* bytes, size_t size, void*) {
    assert(std::string_view(static_cast<const char*>(bytes), size) == "test-secret");
    ++rendered;
    credentials_visible = true;
    return render_ok;
}
void cancel(void*) {
    ++cancelled;
    credentials_visible = false;
}
void wiped() {
    assert(mosaic::ui::slot.token == 0);
    for (auto byte : mosaic::ui::slot.bytes)
        assert(byte == 0);
}
}
int64_t esp_timer_get_time() {
    return clock_us;
}
void vTaskDelay(unsigned ticks) {
    if (delay_waiter_past_deadline) {
        delay_waiter_past_deadline = false;
        const int64_t deadline = clock_us + mosaic::ui::kTimeoutUs;
        const int ordinary = routine_calls;
        clock_us = deadline - 1'000;
        step(); // Matching flush/refresh just before expiry, waiter still paused.
        const auto token = mosaic::ui::rendered_token;
        assert(token && mosaic::ui::gate.acknowledged(token, mosaic::ui::now_ms()));
        assert(credentials_visible);
        clock_us += 500;
        step(); // Worker runs again while acknowledgement is only provisional.
        assert(mosaic::ui::rendered_token == token);
        assert(routine_calls == ordinary);
        clock_us = deadline + 1'000;
        step(); // No waiter commit or cancellation yet, even though time expired.
        assert(mosaic::ui::rendered_token == token);
        assert(routine_calls == ordinary);
        return; // Resume the waiter past its deadline; it must cancel, not succeed.
    }
    clock_us += ticks * 1000;
    if (pump)
        step();
}
lv_timer_t* lv_timer_create(void (*callback)(lv_timer_t*), unsigned, void*) {
    timer_callback = callback;
    static lv_timer_t timer;
    return &timer;
}
void lv_display_add_event_cb(lv_display_t*, void (*callback)(lv_event_t*), int event, void*) {
    if (event == LV_EVENT_FLUSH_FINISH)
        flush_callback = callback;
    else
        refresh_callback = callback;
}
namespace mosaic::drivers::display {
esp_err_t start() {
    return ESP_OK;
}
lv_display_t* handle() {
    static lv_display_t display;
    return &display;
}
bool lock() {
    return true;
}
void unlock() { }
void tick() { }
void note_flush() { }
bool refresh_ready() {
    return ::available && light_ok;
}
bool available() {
    return ::available;
}
void prepare_reboot() {
    ::available = false;
}
}
int main() {
    using namespace mosaic::ui;
    constexpr char secret[] = "test-secret";
    assert(render_credentials(secret, 11) == ESP_ERR_INVALID_STATE);
    assert(start({ create, routine, credentials, cancel, nullptr }) == ESP_OK);
    assert(render_credentials(secret, 11) == ESP_OK);
    wiped();
    step();
    const int ordinary = routine_calls;
    send_refresh = false;
    assert(render_credentials(secret, 11) == ESP_ERR_TIMEOUT);
    // Credential frame blocks all routine dispatch until cancellation.
    assert(routine_calls == ordinary); // waiter cancellation releases routine work on the next worker tick
    wiped();
    step();
    assert(cancelled == 1 && routine_calls > ordinary);
    send_refresh = true;
    send_flush = false;
    assert(render_credentials(secret, 11) == ESP_ERR_TIMEOUT);
    step();
    assert(cancelled == 2);
    send_flush = true;
    light_ok = false;
    assert(render_credentials(secret, 11) == ESP_ERR_TIMEOUT);
    step();
    assert(cancelled == 3);
    light_ok = true;
    pump = false;
    const int before = rendered;
    assert(render_credentials(secret, 11) == ESP_ERR_TIMEOUT);
    wiped();
    step(); // late queued work cannot display wiped/expired credentials
    assert(rendered == before);
    pump = true;
    assert(render_credentials(secret, 11) == ESP_OK); // next token works
    render_ok = false;
    assert(render_credentials(secret, 11) == ESP_ERR_TIMEOUT);
    wiped();
    step(); // rejected renderer can never authorize a refresh
    render_ok = true;
    step(); // Release the prior committed/cancelled frame before this attempt.
    const int before_delayed_cancel = cancelled;
    delay_waiter_past_deadline = true;
    assert(render_credentials(secret, 11) == ESP_ERR_TIMEOUT);
    wiped();
    step();
    assert(cancelled == before_delayed_cancel + 1);
    assert(!credentials_visible && rendered_token == 0);
    assert(render_credentials(secret, 11) == ESP_OK); // next request still works
    waiting.store(true);
    assert(render_credentials(secret, 11) == ESP_ERR_INVALID_STATE);
    waiting.store(false);
    assert(render_credentials(secret, 321) == ESP_ERR_INVALID_ARG);
    mosaic::drivers::display::prepare_reboot();
    assert(!ready());
    assert(render_credentials(secret, 11) == ESP_ERR_INVALID_STATE);
}
