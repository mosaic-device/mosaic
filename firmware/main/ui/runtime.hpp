#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
#include "lvgl.h"
namespace mosaic::ui {
// Borrowed for the process lifetime. All callbacks run under the LVGL worker lock.
// Routine data lives in the renderer's bounded mailbox. Sensitive request bytes
// are copied here, never in that mailbox, and erased on all completion paths.
// A false credential-render result cancels its token; refresh cannot authorize it.
struct Renderer {
    bool (*create)(lv_obj_t*, void*);
    void (*routine)(void*);
    bool (*credentials)(const void*, size_t, void*);
    void (*cancel_credentials)(void*);
    void* context;
};
esp_err_t start(Renderer renderer);
bool ready();
// Application-task only. One caller; two-second token/flush/refresh/backlight gate.
esp_err_t render_credentials(const void* payload, size_t size);
}
