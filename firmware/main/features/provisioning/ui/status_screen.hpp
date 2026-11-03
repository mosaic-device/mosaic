#pragma once

#include "../presentation/view_state.hpp"
#include "../presentation/ui_action.hpp"
#include "lvgl.h"

namespace mosaic::features::provisioning {

// Boot and the setup states other than the QR: a page with the wordmark, a heading,
// what is happening, and "Enter setup mode" when it applies. Before provisioning reports
// its first state, only the wordmark shows.
struct StatusScreen {
    lv_obj_t* card = nullptr;
    lv_obj_t* boot = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* heading = nullptr;
    lv_obj_t* description = nullptr;
    lv_obj_t* recovery_button = nullptr;
    lv_obj_t* recovery_label = nullptr;
    ActionCallback action_callback = nullptr;
    void* action_context = nullptr;

    bool create(lv_obj_t* parent);
    void hide();
    void render(const ViewState& state);
    void bind_action(ActionCallback callback, void* context);

private:
    static void on_recovery(lv_event_t* event);
};

} // namespace mosaic::features::provisioning
