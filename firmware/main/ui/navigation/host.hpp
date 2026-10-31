#pragma once
#include "navigator.hpp"
#include "../runtime.hpp"
#include "lvgl.h"
namespace mosaic::ui::navigation {
// A screen the host shows and hides. Callbacks run in the LVGL context.
struct Page {
    // Builds the screen inside its own container. Focusable widgets created here join
    // the screen's focus group, which the encoder uses while the screen is visible.
    bool (*create)(lv_obj_t* container, void* context) = nullptr;
    // Every UI tick, visible or not, so the screen can follow its feature's state. Optional.
    void (*update)(void* context) = nullptr;
    void* context = nullptr;
};
struct Screens {
    // Provisioning's renderer. It is shown for boot and setup, its routine runs every
    // tick, and it keeps the setup credentials handshake: credentials always bring
    // its screen to the front before they render.
    Renderer setup;
    Page home;
    Page settings;
    // M3: leave empty until the notification screen exists; opening it fails until then.
    Page notification;
    // Read every tick in the LVGL context, after setup's routine.
    Conditions (*conditions)(void* context) = nullptr;
    void* conditions_context = nullptr;
};
// The runtime renderer hosting every screen, for ui::start(). Call once; the runtime
// creates the screens once.
Renderer renderer(const Screens& screens);
// LVGL context, such as a control's callback. False when unavailable from the active
// screen; nothing changes.
bool open(Screen screen);
bool back();
}
