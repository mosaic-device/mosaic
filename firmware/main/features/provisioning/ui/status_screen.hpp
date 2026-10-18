#pragma once

#include "../presentation/view_state.hpp"
#include "../presentation/ui_action.hpp"
#include "lvgl.h"

namespace mosaic::features::provisioning {

struct StatusScreen {
    lv_obj_t* card = nullptr;
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
